# Copyright 2026 Arm Limited and/or its affiliates.
# Mathematical conventions derived from DDSP, Copyright 2026 The DDSP Authors.
# SPDX-License-Identifier: Apache-2.0
"""Host NumPy oracle for MIDI-DDSP waveform processing, not firmware playback.

Inputs are raw network controls, Hz, explicit white noise and a learned signed
reverb impulse. The deterministic noise input makes independent CPU/reference
comparison possible without assuming that TensorFlow and firmware RNGs match.
Only the firmware implementation may establish embedded speed or real-time fit.
"""
import numpy as np

SAMPLE_RATE = 16000
HOP = 64
HARMONICS = 60
NOISE_BINS = 65


def midi_to_hz(notes, midi_zero_silence=True):
    notes = np.asarray(notes, dtype=np.float32)
    if not np.all(np.isfinite(notes)):
        raise ValueError("MIDI pitches must be finite")
    hz = np.float32(440.) * np.exp2((notes - np.float32(69.)) / np.float32(12.))
    # Upstream tests equality to zero, not note <= 0. A sampled negative rest
    # residual therefore has a small positive frequency in the original path.
    return np.where(notes == 0., np.float32(0.), hz) if midi_zero_silence else hz


def exp_sigmoid(values):
    x = np.asarray(values, dtype=np.float64)
    sigmoid = np.exp(-np.logaddexp(0.0, -x))
    return (2.0 * sigmoid ** np.log(10.0) + 1e-7).astype(np.float32)


def scale_controls(raw, f0_hz):
    raw = np.asarray(raw, dtype=np.float32)
    f0 = np.asarray(f0_hz, dtype=np.float32).reshape(-1, 1)
    if raw.ndim != 2 or raw.shape[1] != 1 + HARMONICS + NOISE_BINS or raw.shape[0] != len(f0):
        raise ValueError("Expected raw controls [frames,126] and one f0 per frame")
    if not np.all(np.isfinite(raw)) or not np.all(np.isfinite(f0)) or np.any(f0 < 0):
        raise ValueError("Controls must be finite and f0 must be nonnegative")
    amplitudes = exp_sigmoid(raw[:, :1])
    distribution = exp_sigmoid(raw[:, 1:1 + HARMONICS])
    frequencies = f0 * np.arange(1, HARMONICS + 1, dtype=np.float32)
    distribution[frequencies >= SAMPLE_RATE / 2] = 0
    total = distribution.sum(axis=1, keepdims=True)
    distribution /= np.where(total == 0, np.float32(1e-7), total)
    noise = exp_sigmoid(raw[:, 1 + HARMONICS:] - 5)
    return amplitudes, distribution, noise


def interpolate_frames(values, window=False):
    """DDSP forward/held-endpoint interpolation at 64 samples per interval."""
    values = np.asarray(values, dtype=np.float32)
    if values.ndim != 2 or not len(values):
        raise ValueError("Expected a nonempty [frames,channels] array")
    fraction = np.arange(HOP, dtype=np.float64) / HOP
    if window:
        fraction = 0.5 - 0.5 * np.cos(np.pi * fraction)
    following = np.concatenate((values[1:], values[-1:]), axis=0)
    samples = values[:, None, :] * (1 - fraction[None, :, None]) + following[:, None, :] * fraction[None, :, None]
    return samples.reshape(-1, values.shape[1]).astype(np.float32)


def harmonic_audio(amplitudes, distribution, f0_hz, phase_mode="precise"):
    harmonic = np.arange(1, HARMONICS + 1, dtype=np.float32)
    frequencies = interpolate_frames(np.asarray(f0_hz, dtype=np.float32).reshape(-1, 1) * harmonic)
    envelope = interpolate_frames(np.asarray(amplitudes) * np.asarray(distribution), window=True)
    envelope[frequencies >= SAMPLE_RATE / 2] = 0
    if phase_mode == "precise":
        # Mathematical oracle: avoid the source's float32 accumulation drift.
        phase = np.cumsum(frequencies.astype(np.float64) * (2 * np.pi / SAMPLE_RATE), axis=0)
    elif phase_mode == "ddsp_float32":
        # Also reproduce the source's chunk/modulo operation order explicitly,
        # so long-window source fidelity is checked independently of precision.
        tau = np.float32(2 * np.pi)
        omega = (frequencies * tau) / np.float32(SAMPLE_RATE)
        count = len(omega)
        padded = np.pad(omega, ((0, (-count) % 1000), (0, 0)))
        chunks = np.cumsum(padded.reshape(-1, 1000, HARMONICS), axis=1, dtype=np.float32)
        offsets = np.concatenate((np.zeros_like(chunks[:1, -1:]), chunks[:-1, -1:] % tau), axis=0)
        offsets = np.cumsum(offsets, axis=0, dtype=np.float32) % tau
        phase = ((chunks + offsets) % tau).reshape(-1, HARMONICS)[:count]
    else:
        raise ValueError("Expected phase_mode precise or ddsp_float32")
    return np.sum(np.sin(phase) * envelope, axis=1).astype(np.float32)


def noise_impulses(magnitudes):
    magnitudes = np.asarray(magnitudes, dtype=np.float32)
    if magnitudes.ndim != 2 or magnitudes.shape[1] != NOISE_BINS:
        raise ValueError("Expected [frames,65] noise magnitudes")
    ir_size = 2 * (NOISE_BINS - 1)
    impulse = np.fft.irfft(magnitudes, n=ir_size, axis=-1)
    window = 0.5 - 0.5 * np.cos(2 * np.pi * np.arange(ir_size) / ir_size)
    # Upstream passes sample_rate as positional window_size=16000, which is
    # clamped to ir_size=128. The periodic window is shifted into zero phase,
    # applied there, then the IR is shifted back into causal form.
    impulse *= np.fft.fftshift(window)
    return np.fft.fftshift(impulse, axes=-1).astype(np.float32)


def filtered_noise(magnitudes, white_noise):
    impulses = noise_impulses(magnitudes)
    white_noise = np.asarray(white_noise, dtype=np.float32).reshape(-1)
    if len(white_noise) != len(impulses) * HOP or not np.all(np.isfinite(white_noise)):
        raise ValueError("Provide exactly 64 finite white-noise samples per frame")
    output = np.zeros(len(white_noise) + 256, dtype=np.float64)
    for index, impulse in enumerate(impulses):
        offset = index * HOP
        block = np.convolve(white_noise[offset:offset + HOP], impulse)
        output[offset:offset + len(block)] += block
    delay = (impulses.shape[1] - 1) // 2 - 1  # Original DDSP group-delay crop: 62.
    return output[delay:delay + len(white_noise)].astype(np.float32)


def reverb_impulse(learned):
    learned = np.asarray(learned, dtype=np.float32).reshape(-1)
    if len(learned) != 48000 or not np.all(np.isfinite(learned)):
        raise ValueError("Expected one finite 48000-sample learned signed reverb IR")
    envelope = np.concatenate((np.ones(16000), np.exp(-4 * np.linspace(0, 1, 32000))))
    impulse = (learned * envelope).astype(np.float32)
    impulse[0] = 0  # The dry signal is added separately with unity gain.
    return impulse


def apply_reverb(dry, learned):
    dry = np.asarray(dry, dtype=np.float32).reshape(-1)
    impulse = reverb_impulse(learned)
    fft_size = 1 << (len(dry) + len(impulse) - 2).bit_length()
    wet = np.fft.irfft(np.fft.rfft(dry, fft_size) * np.fft.rfft(impulse, fft_size), fft_size)
    return (dry + wet[:len(dry)]).astype(np.float32)


def render(raw, f0_hz, white_noise, learned_reverb=None):
    amplitudes, distribution, magnitudes = scale_controls(raw, f0_hz)
    dry = harmonic_audio(amplitudes, distribution, f0_hz) + filtered_noise(magnitudes, white_noise)
    return dry if learned_reverb is None else apply_reverb(dry, learned_reverb)
