# Copyright 2026 Arm Limited and/or its affiliates.
# Architecture and weights from DTLN-aec, Copyright (c) 2020 Nils L. Westhausen.
# SPDX-License-Identifier: MIT
"""Fixed single-frame reconstruction of the two published 128-unit stages.

Inputs signal, state, loopback. State is [1,2,128,2], last axis h,c.
Stage 1 consumes magnitudes and returns a mask. Stage 2 consumes time samples
and returns the enhanced 512-sample block. All boundaries are float32.
"""
from pathlib import Path
import torch
from torch import nn
from torch.nn import functional as F
from safetensors.torch import load_file


class InstantNormalization(nn.Module):
    def forward(self, x, gamma, beta):
        centered = x - x.mean(-1, keepdim=True)
        variance = centered.square().mean(-1, keepdim=True)
        return centered * torch.rsqrt(variance + 1e-7) * gamma + beta


class LogPower(nn.Module):
    def forward(self, x):
        return torch.log(x.square() + 1e-7)


class DTLNStage(nn.Module):
    def __init__(self, stage):
        super().__init__()
        self.stage = stage
        self.width = 257 if stage == 1 else 512
        self.normalize = InstantNormalization()
        self.log_power = LogPower()
        for name, tensor in load_file(Path(__file__).parent / f'weights_{stage}.safetensors').items():
            self.register_buffer(name, tensor)

    def forward(self, signal, state, loopback):
        x = signal.reshape(1, self.width)
        ref = loopback.reshape(1, self.width)
        if self.stage == 1:
            x = self.log_power(x)
            ref = self.log_power(ref)
        else:
            x = F.linear(x, self.encoder)
            ref = F.linear(ref, self.encoder)
        encoded = x
        x = self.normalize(x, self.gamma1, self.beta1)
        ref = self.normalize(ref, self.gamma2, self.beta2)
        x = torch.cat((x, ref), dim=-1)
        gates1 = F.linear(x, self.wi1) + F.linear(state[:, 0, :, 0], self.wh1) + self.b1
        i1, f1, g1, o1 = gates1.chunk(4, dim=-1)
        c1 = f1.sigmoid() * state[:, 0, :, 1] + i1.sigmoid() * g1.tanh()
        h1 = o1.sigmoid() * c1.tanh()
        gates2 = F.linear(h1, self.wi2) + F.linear(state[:, 1, :, 0], self.wh2) + self.b2
        i2, f2, g2, o2 = gates2.chunk(4, dim=-1)
        c2 = f2.sigmoid() * state[:, 1, :, 1] + i2.sigmoid() * g2.tanh()
        h2 = o2.sigmoid() * c2.tanh()
        mask = F.linear(h2, self.output_weight, self.output_bias).sigmoid()
        output = mask if self.stage == 1 else F.linear(encoded * mask, self.decoder)
        next_state = torch.stack((torch.stack((h1, h2), dim=1), torch.stack((c1, c2), dim=1)), dim=-1)
        return output.reshape(1, 1, self.width), next_state
