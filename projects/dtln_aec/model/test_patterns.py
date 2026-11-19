# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Deterministic five-second paired fake source for a 16 kHz board demo.

Five one-second sections are silence, echo only, double talk, near end only,
and silence. Echo delays are 53 and 104 samples. Input amplitudes are PCM
full-scale fractions. The clean near-end stream supports residual metrics.
"""
import numpy as np

SAMPLE_RATE = 16000
MODES = ('silence', 'echo only', 'double talk', 'near end only', 'silence')


def voiced(index, period, harmonics):
    phase = 2.0 * np.pi * (index % period) / period
    envelope = 0.4 + 0.6 * (1.0 - np.abs(index % 4096 - 2048) / 2048)
    return envelope * sum(weight * np.sin(frequency * phase) for frequency, weight in harmonics)


def patterns():
    sample = np.arange(len(MODES) * SAMPLE_RATE)
    far = np.where((sample >= SAMPLE_RATE) & (sample < 3 * SAMPLE_RATE),
        voiced(sample, 127, [(1, .16), (2, .09), (4, .05), (7, .025)]), 0).astype(np.float32)
    near = np.where((sample >= 2 * SAMPLE_RATE) & (sample < 4 * SAMPLE_RATE),
        voiced(sample, 89, [(1, .13), (3, .06), (4, .04), (8, .025)]), 0).astype(np.float32)
    echo = .58 * np.pad(far, (53, 0))[:-53] + .21 * np.pad(far, (104, 0))[:-104]
    return near + echo, far, near
