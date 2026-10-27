# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""FARGAN reconstruction from the rvTTS/Opus BSD-licensed parameter arrays."""
from pathlib import Path
from functools import lru_cache
import re
import numpy as np
import torch
from torch import nn
import torch.nn.functional as F

HERE = Path(__file__).resolve().parent


@lru_cache(maxsize=1)
def load_arrays():
    text = (HERE / "upstream/fargan_data.c").read_text()
    return {name: np.fromstring(re.sub(r'(?<=[0-9])f\b', '', values), sep=',', dtype=np.float32)
            for name, values in re.findall(r'const\s+\w+\s+(\w+)\[\d+\]\s*=\s*\{(.*?)\};', text, re.S)}


def linear(arrays, name, inputs, outputs):
    layer = nn.Linear(inputs, outputs, bias=(name+"_bias") in arrays)
    weight = arrays[name+"_weights_float"].reshape(inputs, outputs).T.copy()
    layer.weight = nn.Parameter(torch.from_numpy(weight), requires_grad=False)
    if layer.bias is not None:
        layer.bias = nn.Parameter(torch.from_numpy(arrays[name+"_bias"].copy()), requires_grad=False)
    return layer


def inspect_arrays():
    arrays = load_arrays()
    results = {}
    for name in list(arrays):
        if not name.endswith("_scale"):
            continue
        stem = name.removesuffix("_scale")
        rows = len(arrays[name])
        cols = len(arrays[stem+"_weights_int8"]) // rows
        weights = arrays[stem+"_weights_int8"].reshape(rows//8, cols//4, 8, 4).transpose(0, 2, 1, 3).reshape(rows, cols)
        dequantized = weights * arrays[name][:, None] * 127
        floating = arrays[stem+"_weights_float"].reshape(cols, rows).T
        results[stem] = float(np.max(np.abs(floating-dequantized)))
    return results


STATE = 964
INPUT = 32 + STATE
OUTPUT = 160 + STATE


class Fargan(nn.Module):
    """Whole 10 ms FARGAN network with explicit source state.

    Pitch index generation, embedding lookup and output de-emphasis are the
    source's non-neural DSP/frontend operations. ET chooses all partitions of
    this complete four-subframe neural frame.
    """
    def __init__(self):
        super().__init__()
        a = load_arrays()
        dims = {"cond_net_fdense1": (32, 64), "cond_net_fconv1": (192, 128),
                "cond_net_fdense2": (128, 320), "sig_net_cond_gain_dense": (80, 1),
                "sig_net_fwc0_conv": (328, 192), "sig_net_fwc0_glu_gate": (192, 192),
                "sig_net_gru1_input": (272, 480), "sig_net_gru1_recurrent": (160, 480),
                "sig_net_gru2_input": (240, 384), "sig_net_gru2_recurrent": (128, 384),
                "sig_net_gru3_input": (208, 384), "sig_net_gru3_recurrent": (128, 384),
                "sig_net_gru1_glu_gate": (160, 160), "sig_net_gru2_glu_gate": (128, 128),
                "sig_net_gru3_glu_gate": (128, 128), "sig_net_skip_glu_gate": (128, 128),
                "sig_net_skip_dense": (688, 128), "sig_net_sig_dense_out": (128, 40),
                "sig_net_gain_dense_out": (192, 4)}
        self.layers = nn.ModuleDict({name: linear(a, name, *shape) for name, shape in dims.items()})

    def condition(self, x, memory):
        pre = torch.tanh(self.layers["cond_net_fdense1"](x))
        conv_input = torch.cat((memory, pre), 1)
        cond = torch.tanh(self.layers["cond_net_fconv1"](conv_input))
        cond = torch.tanh(self.layers["cond_net_fdense2"](cond))
        return cond, conv_input[:, 64:]

    def gru(self, index, x, h):
        gates = self.layers[f"sig_net_gru{index}_input"](x)
        recur = self.layers[f"sig_net_gru{index}_recurrent"](h)
        size = h.shape[1]
        z = torch.sigmoid(gates[:, :size] + recur[:, :size])
        r = torch.sigmoid(gates[:, size:2*size] + recur[:, size:2*size])
        candidate = torch.tanh(gates[:, 2*size:] + r * recur[:, 2*size:])
        state = z * h + (1-z) * candidate
        output = state * torch.sigmoid(self.layers[f"sig_net_gru{index}_glu_gate"](state))
        return output, state

    def subframe(self, cond, pitch, memory, h1, h2, h3, indices):
        gain = torch.exp(self.layers["sig_net_cond_gain_dense"](cond))
        pred = (torch.gather(pitch, 1, indices) / (gain + 1e-5)).clamp(-1, 1)
        prev = (pitch[:, -40:] / (gain + 1e-5)).clamp(-1, 1)
        x = torch.cat((cond, pred, prev), 1)
        fwc = torch.tanh(self.layers["sig_net_fwc0_conv"](torch.cat((memory, x), 1)))
        fwc = fwc * torch.sigmoid(self.layers["sig_net_fwc0_glu_gate"](fwc))
        gates = torch.sigmoid(self.layers["sig_net_gain_dense_out"](fwc))
        center = pred[:, 2:42]
        out1, h1 = self.gru(1, torch.cat((fwc, gates[:, :1] * center, prev), 1), h1)
        out2, h2 = self.gru(2, torch.cat((out1, gates[:, 1:2] * center, prev), 1), h2)
        out3, h3 = self.gru(3, torch.cat((out2, gates[:, 2:3] * center, prev), 1), h3)
        skip = torch.cat((out1, out2, out3, fwc, gates[:, 3:4] * center, prev), 1)
        skip = torch.tanh(self.layers["sig_net_skip_dense"](skip))
        skip = skip * torch.sigmoid(self.layers["sig_net_skip_glu_gate"](skip))
        excitation = torch.tanh(self.layers["sig_net_sig_dense_out"](skip)) * gain
        pitch = torch.cat((pitch[:, 40:], excitation), 1)
        return excitation, pitch, x, h1, h2, h3

    def forward(self, packed, indices):
        features, state = packed[:, :32], packed[:, 32:]
        pitch, cm, fm = state[:, :256], state[:, 256:384], state[:, 384:548]
        h1, h2, h3 = state[:, 548:708], state[:, 708:836], state[:, 836:964]
        cond, cm = self.condition(features, cm)
        excitation = []
        for subframe in range(4):
            pcm, pitch, fm, h1, h2, h3 = self.subframe(cond[:, subframe*80:(subframe+1)*80], pitch, fm, h1, h2, h3, indices)
            excitation.append(pcm)
        return torch.cat((*excitation, pitch, cm, fm, h1, h2, h3), 1)


class FarganInit(Fargan):
    def forward(self, features, indices, zero_state):
        pre = torch.tanh(self.layers["cond_net_fdense1"](features))
        cm = torch.cat((pre, pre), 1)
        cond = torch.tanh(self.layers["cond_net_fconv1"](torch.cat((pre, pre, pre), 1)))
        cond = torch.tanh(self.layers["cond_net_fdense2"](cond))
        # Keep the initial zeros at the graph boundary. The integer TOSA
        # serializer cannot embed a floating constant in this partition.
        pitch = zero_state[:, :256]
        fm = zero_state[:, 384:548]
        h1, h2, h3 = zero_state[:, 548:708], zero_state[:, 708:836], zero_state[:, 836:964]
        for subframe in range(4):
            _, _, fm, h1, h2, h3 = self.subframe(cond[:, subframe*80:(subframe+1)*80], pitch, fm, h1, h2, h3, indices)
        return torch.cat((pitch, cm, fm, h1, h2, h3), 1)


def frame_input(features):
    features = np.asarray(features, np.float32)
    period = int(np.floor(.5 + 256.0 / 2.0 ** (float(features[18]) + 1.5)))
    period = max(1, period)
    embedding = load_arrays()["cond_net_pembed_weights_float"].reshape(224, 12)[np.clip(period-32, 0, 223)]
    return torch.from_numpy(np.concatenate((features, embedding))).unsqueeze(0), period


def pitch_indices(period):
    pos = 256 - period - 2
    indices = []
    for _ in range(44):
        indices.append(max(0, pos))
        pos += 1
        if pos == 256:
            pos -= period
    return torch.tensor([indices], dtype=torch.int64)


if __name__ == "__main__":
    print(inspect_arrays())
