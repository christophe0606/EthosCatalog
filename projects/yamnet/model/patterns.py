# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Deterministic synthetic calibration/evaluation waveforms; no AudioSet media."""
import numpy as np
from pathlib import Path


def waveforms(seed, count):
    rng = np.random.default_rng(seed)
    t = np.arange(15600, dtype=np.float32) / 16000
    yield 'silence', np.zeros_like(t)
    yield 'white_noise', rng.uniform(-1, 1, len(t)).astype(np.float32)
    yield 'sine_440', np.sin(2*np.pi*440*t).astype(np.float32)
    for i in range(count-3):
        kind = i % 5
        amp = float(10**rng.uniform(-3.5, -.05))
        if kind == 0:
            y = rng.normal(0, amp, len(t))
        elif kind == 1:
            y = amp*np.sin(2*np.pi*rng.uniform(70, 7400)*t + rng.uniform(0, 6.28))
        elif kind == 2:
            f = rng.uniform(70, 500)
            y = sum(amp/(h**1.3)*np.sin(2*np.pi*h*f*t) for h in range(1, 8))
            y *= .5 + .5*np.sin(2*np.pi*rng.uniform(2,8)*t)
        elif kind == 3:
            y = rng.normal(0, amp, len(t)) * np.exp(-np.mod(t, .12)*30)
        else:
            y = amp*np.sin(2*np.pi*(100*t + rng.uniform(500,3500)*t*t)) + rng.normal(0, amp*.05, len(t))
        yield f'synthetic_{i:03d}', np.clip(y,-1,1).astype(np.float32)


def features(waveform, window=None, mel=None):
    """Exact feature contract, with TF-exported float32 Hann/mel coefficients."""
    if window is None:
        tables = np.load(Path(__file__).with_name('frontend_tables.npz'))
        window, mel = tables['hann'], tables['mel']
    frames = np.lib.stride_tricks.sliding_window_view(waveform,400)[::160]
    spec = np.abs(np.fft.rfft(frames*window,n=512)).astype(np.float32)
    return np.log(spec @ mel + np.float32(.001)).astype(np.float32)[None]
