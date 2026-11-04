# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Validation scores based on the original MIDI fixtures in this project."""
import sys
from pathlib import Path
import numpy as np
import torch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'player'))
from make_demo_midi import BASE_NOTES
from score_features import make_features


def demo_score(instrument, frames, seed):
    if frames != 1000:
        raise ValueError('The eight half-second note/rest pairs require 1000 frames')
    # Quantized version of the demo: 0.4375s held note rounds to109frames,
    # with16rest frames completing each125frame/half-second interval.
    pitches = np.asarray([value for interval in (0, 4, 7, 2, 5, 9, 7, 0)
                          for value in (BASE_NOTES[instrument]+interval, 0)], dtype=np.int64)
    lengths = np.asarray([109, 16]*8, dtype=np.int64)
    duration, mapping, features = make_features(pitches, lengths, np.zeros((16, 6), np.float32))
    generator = torch.Generator().manual_seed(seed)
    return (torch.from_numpy(pitches[None]), torch.from_numpy(duration[None]),
            torch.tensor([instrument]), torch.from_numpy(mapping[None]),
            torch.from_numpy(features[None, :, 6:]),
            torch.rand((1, frames, 1), generator=generator)*.98+.01)
