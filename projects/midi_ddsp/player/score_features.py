# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Host oracle for contiguous, frame-quantized monophonic score segments.

This does not parse MIDI, choose time quantization, or implement firmware.
It defines the preprocessing that must surround the exported learned methods.
"""
import numpy as np


def make_features(pitches, lengths, expression):
    """Return duration seconds, frame-note mapping and synthesis features.

    ``expression`` is the raw, rest-masked expression-generator output. Clipping
    here affects synthesis conditioning only; never feed it back to that RNN.
    Frame features follow upstream's index_length code, including its default
    100-region mask limit. Note/rest segments must be contiguous and nonempty.
    """
    pitches = np.asarray(pitches)
    lengths = np.asarray(lengths)
    expression = np.asarray(expression, dtype=np.float32)
    if (pitches.ndim != 1 or lengths.shape != pitches.shape or len(pitches) == 0
            or expression.shape != (len(pitches), 6)
            or not np.issubdtype(pitches.dtype, np.integer)
            or not np.issubdtype(lengths.dtype, np.integer)
            or np.any(pitches < 0) or np.any(pitches > 127)
            or np.any(lengths <= 0) or not np.isfinite(expression).all()):
        raise ValueError('Expected integer pitches 0..127, positive frame lengths and finite Nx6 expression')
    mapping = np.repeat(np.arange(len(pitches), dtype=np.int64), lengths)
    features = np.zeros((len(mapping), 4), dtype=np.float32)
    features[:, 0] = pitches[mapping].astype(np.float32) / np.float32(127.)
    first = 0
    for region, (pitch, length) in enumerate(zip(pitches, lengths)):
        end = first + int(length)
        features[first, 1] = 1.
        features[end-1, 2] = 1.
        if pitch != 0 and region < 100:
            features[first:end, 3] = np.arange(1, int(length)+1, dtype=np.float32) / np.float32(length)
        first = end
    conditioning = np.clip(expression, 0., 1.)[mapping]
    return lengths.astype(np.float32)[:, None] * np.float32(.004), mapping, np.concatenate([conditioning, features], axis=-1)
