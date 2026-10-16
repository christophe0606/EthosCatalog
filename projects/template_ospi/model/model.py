# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""A signed, elementwise 2x gain; quantize.py selects int16 Ethos arithmetic."""
from torch import nn
INPUT_SHAPE = (1, 1, 512, 1)  # NHWC: one vector of microphone samples.
OUTPUT_SHAPE = INPUT_SHAPE
GAIN = 2.0
class GainModel(nn.Module):
    def forward(self, x):
        return x * GAIN
