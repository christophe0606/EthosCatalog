# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""A signed, sample-wise 2x gain executed by Ethos, with no ReLU or bias."""
import torch
from torch import nn
INPUT_SHAPE = (1, 1, 512, 1)  # NHWC: one vector of microphone samples.
OUTPUT_SHAPE = INPUT_SHAPE
GAIN = 2.0
class GainModel(nn.Module):
    def __init__(self):
        super().__init__()
        self.gain = nn.Conv2d(1, 1, kernel_size=1, bias=False)
        with torch.no_grad():
            self.gain.weight.fill_(GAIN)
    def forward(self, x):
        return self.gain(x.permute(0, 3, 1, 2)).permute(0, 2, 3, 1).contiguous()
