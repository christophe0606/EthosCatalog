# Copyright (c) 2026 Arm Limited or its affiliates. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

"""Deterministic PyTorch starting point for the E8 Ethos-U85 smoke test."""
import torch
from torch import nn

INPUT_SHAPE = (1, 8, 8, 1)  # NHWC at the firmware boundary.
OUTPUT_SHAPE = (1, 8, 8, 2)


class SmokeModel(nn.Module):
    def __init__(self):
        super().__init__()
        self.conv = nn.Conv2d(1, 2, kernel_size=1)
        with torch.no_grad():
            self.conv.weight.copy_(torch.tensor([1.0, -0.5]).reshape(2, 1, 1, 1))
            self.conv.bias.copy_(torch.tensor([0.125, 0.25]))

    def forward(self, x):
        x = x.permute(0, 3, 1, 2)
        return torch.relu(self.conv(x)).permute(0, 2, 3, 1).contiguous()


