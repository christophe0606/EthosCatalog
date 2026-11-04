#!/usr/bin/env python3
# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Compare the NumPy DSP oracle against the unmodified pinned DDSP TF math."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import types
import zlib
import numpy as np
import tensorflow as tf
from reference_synth import (HOP, apply_reverb, exp_sigmoid, filtered_noise,
                             harmonic_audio, interpolate_frames, noise_impulses,
                             reverb_impulse, scale_controls, midi_to_hz)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parent / "reference_validation.json")
    parser.add_argument("--cpu-assets", type=Path, help="Also check all thirteen learned IRs from cpu_assets.bin")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent
    provenance = json.loads((root / "reference/provenance.json").read_text())
    for name, record in provenance["files"].items():
        if hashlib.sha256((root / "reference" / name).read_bytes()).hexdigest() != record["sha256"]:
            raise ValueError(f"Reference source hash changed: {name}")
    # gin only decorates these standalone math functions. Supplying an identity
    # decorator avoids installing the upstream training/dependency package; the
    # vendored source file remains byte-for-byte unchanged and hash checked.
    try:
        import gin  # noqa: F401
    except ImportError:
        gin = types.ModuleType("gin")
        gin.register = gin.configurable = lambda function: function
        sys.modules["gin"] = gin
    spec = importlib.util.spec_from_file_location("pinned_ddsp_core", root / "reference/core.py")
    core = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(core)
    pitches = np.array([-1., -.01, 0., .01, 12., 48., 60., 69., 96., 127.], np.float32)
    conversion_metrics = []
    for zero_silence in (False, True):
        actual = midi_to_hz(pitches, zero_silence)
        expected = core.midi_to_hz(pitches, midi_zero_silence=zero_silence).numpy()
        np.testing.assert_allclose(actual, expected, rtol=2e-7, atol=1e-6)
        conversion_metrics.append({'midi_zero_silence': zero_silence,
                                   'maximum_absolute_error_hz': float(np.max(np.abs(actual - expected)))})
    bandedge_f0 = np.array([0., 7999., 8000., 9000.], np.float32)[:, None]
    bandedge_raw = np.zeros((4, 126), np.float32)
    _, actual_distribution, _ = scale_controls(bandedge_raw, bandedge_f0)
    expected_distribution = core.normalize_harmonics(core.exp_sigmoid(bandedge_raw[None, :, 1:61]),
                                                     bandedge_f0[None], 16000).numpy()[0]
    np.testing.assert_allclose(actual_distribution, expected_distribution, rtol=0, atol=1e-7)
    generator = np.random.default_rng(20261104)
    cases = []
    for frames in (1, 4, 17, 65, 1000):
        raw = generator.normal(-0.5, 1.5, (frames, 126)).astype(np.float32)
        f0 = np.linspace(110, 880, frames, dtype=np.float32).reshape(-1, 1)
        white = generator.uniform(-1, 1, frames * HOP).astype(np.float32)
        amp, hd, noise = scale_controls(raw, f0)
        tf_amp = core.exp_sigmoid(raw[None, :, :1])
        tf_hd = core.normalize_harmonics(core.exp_sigmoid(raw[None, :, 1:61]), f0[None], 16000)
        tf_noise = core.exp_sigmoid(raw[None, :, 61:] - 5)
        expected_harmonic = core.harmonic_synthesis(f0[None], tf_amp, harmonic_distribution=tf_hd,
                                                   n_samples=frames * HOP, use_angular_cumsum=True).numpy()[0]
        expected_noise = core.frequency_filter(white[None], tf_noise, window_size=16000).numpy()[0]
        comparisons = {
            "amplitude": (amp, tf_amp.numpy()[0], 1e-6),
            "harmonic_distribution": (hd, tf_hd.numpy()[0], 1e-6),
            "noise_magnitudes": (noise, tf_noise.numpy()[0], 1e-6),
            "linear_interpolation": (interpolate_frames(f0), core.resample(f0[None], frames * HOP).numpy()[0], 1e-4),
            "hann_interpolation": (interpolate_frames(amp * hd, window=True), core.resample((amp * hd)[None], frames * HOP, method="window").numpy()[0], 1e-6),
            "noise_impulse": (noise_impulses(noise), core.frequency_impulse_response(noise[None], 16000).numpy()[0], 1e-6),
            "filtered_noise": (filtered_noise(noise, white), expected_noise, 1e-6),
            "harmonic_audio": (harmonic_audio(amp, hd, f0, phase_mode="ddsp_float32"), expected_harmonic, 1e-5),
        }
        metrics = {}
        for name, (actual, expected, tolerance) in comparisons.items():
            error = np.abs(actual.astype(np.float64) - expected.astype(np.float64))
            maximum = float(error.max())
            metrics[name] = {"max_abs_error": maximum, "rmse": float(np.sqrt(np.mean(error ** 2))), "tolerance": tolerance}
            if maximum > tolerance:
                raise AssertionError(f"{frames} frames {name}: {maximum} > {tolerance}")
        precise = harmonic_audio(amp, hd, f0)
        error = np.abs(precise.astype(np.float64) - expected_harmonic.astype(np.float64))
        metrics['precise_vs_source_float32_phase'] = {
            'max_abs_error': float(error.max()), 'rmse': float(np.sqrt(np.mean(error**2))),
            'role': 'Measured arithmetic-precision difference; source-aligned waveform checked separately'}
        # A sparse signed IR makes causality, decay, unity dry and first-tap
        # suppression visible independently of the neural checkpoint.
        learned = np.zeros(48000, np.float32)
        learned[[0, 1, 37, 16001, 40000]] = [1, 0.2, -0.1, 0.05, -0.04]
        expected_reverb = expected_harmonic + core.fft_convolve(expected_harmonic[None], reverb_impulse(learned)[None], delay_compensation=0).numpy()[0]
        actual_reverb = apply_reverb(expected_harmonic, learned)
        error = np.abs(actual_reverb - expected_reverb)
        if error.max() > 1e-6:
            raise AssertionError(f"Reverb discrepancy: {error.max()}")
        metrics["reverb"] = {"max_abs_error": float(error.max()), "tolerance": 1e-6}
        cases.append({"frames": frames, "metrics": metrics})
    trained_reverb = []
    if args.cpu_assets:
        asset_data = args.cpu_assets.read_bytes()
        manifest = json.loads(args.cpu_assets.with_suffix('.json').read_text())
        if len(asset_data) != manifest['bytes'] or zlib.crc32(asset_data) != manifest['crc32']:
            raise ValueError('CPU asset size/CRC mismatch')
        record = manifest['tensors']['s/reverb_module/magnitudes_embedding/embeddings']
        if record['shape'] != [13, 48000]:
            raise ValueError('Expected the original thirteen signed reverb rows')
        impulses = np.frombuffer(asset_data, dtype='<f4', count=record['count'], offset=record['offset']).reshape(13, 48000)
        dry = np.zeros(48064, np.float32)
        dry[[0, 1, 16384, 32768]] = [1., -.3, .5, -.7]
        for instrument, learned in enumerate(impulses):
            expected = dry + core.fft_convolve(dry[None], reverb_impulse(learned)[None], delay_compensation=0).numpy()[0]
            actual = apply_reverb(dry, learned)
            maximum = float(np.max(np.abs(actual - expected)))
            if maximum > 1e-5:
                raise AssertionError(f'Learned reverb {instrument}: {maximum}')
            trained_reverb.append({'instrument_id': instrument, 'maximum_absolute_error': maximum, 'tolerance': 1e-5})
    result = {"reference_revision": provenance["revision"], "cases": cases, "passed": True,
              "midi_to_hz": conversion_metrics,
              "nyquist_edge_maximum_error": float(np.max(np.abs(actual_distribution - expected_distribution))),
              "trained_reverb": trained_reverb,
              "scope": "Host mathematical oracle vs pinned unmodified DDSP core; not firmware, timing, trained audio or listening quality"}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(f"DSP reference: PASS ({len(cases)} lengths, controls/interpolation/harmonics/noise/reverb)")


if __name__ == "__main__":
    main()
