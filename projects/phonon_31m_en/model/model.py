# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Trained Phonon q8 reconstruction; source tensors are never randomized.

Architecture follows Gradium xn-ptts v0.4.0, dual MIT OR Apache-2.0;
this adaptation selects Apache-2.0. See ../NOTICE.md and ../CONVERSION.md.
All 185 source tensors are retained in the source safetensors. The runtime
omits the unused training-only flow.w_s_t head, as upstream's loader does.
"""
from __future__ import annotations

from pathlib import Path
import torch
from torch import nn
from torch.nn import functional as F
from safetensors.torch import load_file

ROOT = Path(__file__).resolve().parent


class Weights(nn.Module):
    def __init__(self, state):
        super().__init__()
        for name, value in state.items():
            self.register_buffer(name.replace(".", "__"), value)

    def w(self, name):
        return getattr(self, name.replace(".", "__"))

    def linear(self, x, name):
        return F.linear(x, self.w(name + ".weight"),
                        getattr(self, (name + ".bias").replace(".", "__"), None))

    def norm(self, x, name, eps=1e-5):
        return layer_norm(x, eps) * self.w(name + ".weight") + self.w(name + ".bias")


def layer_norm(x, eps):
    # Exact LayerNorm formula, expressed without aten.var: the installed
    # ExecuTorch DecomposeVarPass fails on the PyTorch 2.13 SymFloat epsilon.
    centred = x - x.mean(-1, keepdim=True)
    return centred * torch.rsqrt((centred * centred).mean(-1, keepdim=True) + eps)


def silu(x):
    # The installed Cortex-M portable runtime has sigmoid/mul but no SiLU.
    return x * torch.sigmoid(x)


def rope(x, cos, sin):
    # xn's rope_i uses adjacent real/imag pairs (interleaved RoPE).
    x = x.reshape(*x.shape[:-1], 32, 2)
    a, b = x[..., 0], x[..., 1]
    return torch.stack((a * cos - b * sin, a * sin + b * cos), -1).flatten(-2)


class Backbone(Weights):
    """Streaming transformer with explicit rolling KV state.

    `cache`: [layers, 2, 1, 8, capacity, 64]. Invalid leading rows are
    masked. Flow context must not exceed capacity; Mimi has a rolling window.
    `position` always holds the absolute offset, including for Mimi RoPE.
    """
    def __init__(self, state, prefix="flow_lm.transformer", layers=6, capacity=512):
        super().__init__({k: v for k, v in state.items() if k.startswith(prefix + ".")})
        self.prefix, self.layers, self.capacity = prefix, layers, capacity
        self.register_buffer("inv_freq", 10000.0 ** (-torch.arange(32, dtype=torch.float32) / 32))

    def forward(self, x, cache, position):
        length = x.shape[1]
        queries = torch.arange(length, dtype=torch.float32)
        angles = (position + queries).reshape(1, 1, length, 1) * self.inv_freq
        cos, sin = angles.cos(), angles.sin()
        indices = torch.arange(self.capacity + length, dtype=torch.float32)
        valid = (indices >= self.capacity - position) & (indices <= self.capacity + queries[:, None])
        if self.prefix.startswith("mimi."):
            valid = valid & (indices >= queries[:, None])
        mask = torch.where(valid,
                           torch.tensor(0.0), torch.tensor(-1.0e9))
        states = []
        for i in range(self.layers):
            p = f"{self.prefix}.layers.{i}"
            q, k, v = self.linear(self.norm(x, p + ".norm1"), p + ".self_attn.in_proj").chunk(3, -1)
            q = rope(q.reshape(1, length, 8, 64).transpose(1, 2), cos, sin)
            k = rope(k.reshape(1, length, 8, 64).transpose(1, 2), cos, sin)
            v = v.reshape(1, length, 8, 64).transpose(1, 2)
            keys = torch.cat((cache[i, 0], k), dim=2)
            values = torch.cat((cache[i, 1], v), dim=2)
            attention = torch.softmax((q @ keys.transpose(-2, -1)) * 0.125 + mask, dim=-1)
            attended = (attention @ values).transpose(1, 2).reshape(1, length, 512)
            out = self.linear(attended, p + ".self_attn.out_proj")
            scale1 = getattr(self, (p + ".layer_scale_1.scale").replace(".", "__"), None)
            if scale1 is not None:
                out = out * scale1
            x = x + out
            out = self.linear(F.gelu(self.linear(self.norm(x, p + ".norm2"), p + ".linear1")), p + ".linear2")
            scale2 = getattr(self, (p + ".layer_scale_2.scale").replace(".", "__"), None)
            if scale2 is not None:
                out = out * scale2
            x = x + out
            states.append(torch.stack((keys[:, :, -self.capacity:, :], values[:, :, -self.capacity:, :]), dim=0))
        return x, torch.stack(states, dim=0)


class FlowHead(Weights):
    def __init__(self, state):
        super().__init__({k: v for k, v in state.items() if
                          k.startswith("flow_lm.flow_net.") or k.startswith("flow_lm.out_")})

    def time_embedding(self, i, time):
        p = f"flow_lm.flow_net.time_embed.{i}"
        angle = time * self.w(p + ".freqs")
        x = self.linear(torch.cat((angle.cos(), angle.sin()), -1), p + ".mlp.0")
        x = self.linear(silu(x), p + ".mlp.2")
        return x * torch.rsqrt((x * x).mean(-1, keepdim=True) + 1e-5) * self.w(p + ".mlp.3.alpha")

    def forward(self, backbone_out, noise):
        c = self.norm(backbone_out, "flow_lm.out_norm").reshape(1, 512)
        eos = self.linear(c, "flow_lm.out_eos")
        p = "flow_lm.flow_net"
        # This checkpoint has exactly one LSD step: s=0, t=1.
        y = (self.time_embedding(0, torch.zeros_like(noise[:, :1])) +
             self.time_embedding(1, torch.ones_like(noise[:, :1]))) * 0.5
        y = y + self.linear(c, p + ".cond_embed")
        x = self.linear(noise, p + ".input_proj")
        for i in range(6):
            r = p + f".res_blocks.{i}"
            shift, scale, gate = self.linear(silu(y), r + ".adaLN_modulation.1").chunk(3, -1)
            h = self.norm(x, r + ".in_ln", 1e-6) * (1.0 + scale) + shift
            h = self.linear(silu(self.linear(h, r + ".mlp.0")), r + ".mlp.2")
            x = x + gate * h
        p += ".final_layer"
        shift, scale = self.linear(silu(y), p + ".adaLN_modulation.1").chunk(2, -1)
        x = layer_norm(x, 1e-6) * (1.0 + scale) + shift
        return noise + self.linear(x, p + ".linear"), eos


class AudioDecoder(Weights):
    """Complete Mimi decoder with explicit attention and convolution state."""
    def __init__(self, state, capacity=250):
        super().__init__({k: v for k, v in state.items() if
                          (k.startswith("mimi.") and not k.startswith("mimi.decoder_transformer.")) or k in
                          ("flow_lm.emb_mean", "flow_lm.emb_std")})
        self.backbone = Backbone(state, "mimi.decoder_transformer.transformer", 2, capacity)
        self.capacity = capacity

    @staticmethod
    def state_shapes():
        return [(1, 512, 16), (1, 512, 6), (1, 256, 6),
                (1, 256, 2), (1, 128, 0), (1, 128, 5),
                (1, 128, 2), (1, 64, 0), (1, 64, 4),
                (1, 64, 2), (1, 32, 0), (1, 64, 2)]

    def conv(self, x, name, previous):
        size = previous.shape[-1]
        if size:
            x = torch.cat((previous, x), -1)
        y = F.conv1d(x, self.w(name + ".weight"), getattr(self, (name + ".bias").replace(".", "__"), None))
        return y, x[..., -size:] if size else previous

    def convtr(self, x, name, previous, stride, groups=1):
        bias = getattr(self, (name + ".bias").replace(".", "__"), None)
        # Expose the exact singleton-height 2D equivalent to the Arm quantizer;
        # conv_transpose1d is otherwise left as an unquantized CPU convolution.
        y = F.conv_transpose2d(x.unsqueeze(2), self.w(name + ".weight").unsqueeze(2),
                               bias, stride=(1, stride), groups=groups).squeeze(2)
        size = previous.shape[-1]
        y = torch.cat((y[..., :size] + previous, y[..., size:]), -1)
        partial = y[..., -size:]
        if bias is not None:
            partial = partial - bias.reshape(1, -1, 1)
        return y[..., :-size], partial

    def forward(self, latent, cache, position, states):
        latent = latent.reshape(1, 32, 1) * self.w("flow_lm.emb_std").reshape(1, 32, 1)
        latent = latent + self.w("flow_lm.emb_mean").reshape(1, 32, 1)
        x = F.conv1d(latent, self.w("mimi.quantizer.output_proj.weight"))
        x, st0 = self.convtr(x, "mimi.upsample.convtr.convtr", states[0], 16, 512)
        x, cache = self.backbone(x.transpose(1, 2), cache, position)
        x = x.transpose(1, 2)
        result = [st0]
        x, st = self.conv(x, "mimi.decoder.model.0.conv", states[1]); result.append(st)
        cursor = 2
        for up, res, stride in ((2, 3, 6), (5, 6, 5), (8, 9, 4)):
            x, st = self.convtr(F.elu(x), f"mimi.decoder.model.{up}.convtr", states[cursor], stride)
            result.append(st); cursor += 1
            h, st = self.conv(F.elu(x), f"mimi.decoder.model.{res}.block.1.conv", states[cursor])
            result.append(st); cursor += 1
            h, st = self.conv(F.elu(h), f"mimi.decoder.model.{res}.block.3.conv", states[cursor])
            result.append(st); cursor += 1
            x = x + h
        x, st = self.conv(F.elu(x), "mimi.decoder.model.11.conv", states[cursor]); result.append(st)
        return x, cache, tuple(result)


class Phonon(nn.Module):
    def __init__(self, state, flow_capacity=512):
        super().__init__()
        self.source = Weights({k: v for k, v in state.items() if k.startswith("flow_lm.condition_provider.") or
                               k.startswith("flow_lm.input_linear.") or k == "flow_lm.bos_emb"})
        self.backbone = Backbone(state, capacity=flow_capacity)
        self.flow = FlowHead(state)
        self.decoder = AudioDecoder(state)
        self.flow_capacity = flow_capacity
        p = "flow_lm.condition_provider.conditioners."
        ns = self.source.linear(self.source.w(p + "num_speakers.embed.weight")[1:2], p + "num_speakers.output_proj")
        duration = torch.cat((torch.ones(1, 64), torch.zeros(1, 64)), -1)
        self.register_buffer("conditions", ns + self.source.linear(duration, p + "duration_delta.output_proj"))

    def initial_state(self):
        return (torch.zeros(6, 2, 1, 8, self.flow_capacity, 64),
                torch.zeros(2, 2, 1, 8, 250, 64),
                tuple(torch.zeros(shape) for shape in AudioDecoder.state_shapes()))

    def forward(self, previous_latent, noise, flow_cache, flow_position, mimi_cache, mimi_position, audio_states):
        x = self.source.linear(previous_latent.reshape(1, 1, 32), "flow_lm.input_linear") + self.conditions
        x, flow_cache = self.backbone(x, flow_cache, flow_position)
        latent, eos = self.flow(x, noise)
        audio, mimi_cache, audio_states = self.decoder(latent, mimi_cache, mimi_position, audio_states)
        return latent, eos, audio, flow_cache, mimi_cache, audio_states

    def prompt(self, embeddings, cache, position):
        for i in range(embeddings.shape[1]):
            if position >= self.flow_capacity:
                raise ValueError("Flow KV capacity exhausted; split text before inference")
            _, cache = self.backbone(embeddings[:, i:i + 1], cache, torch.tensor(float(position)))
            position += 1
        return cache, position

    def text_embedding(self, tokens):
        return F.embedding(tokens, self.source.w("flow_lm.condition_provider.conditioners.transcript_in_segment.embed.weight"))


def load_model(weights=None, flow_capacity=512):
    path = Path(weights) if weights else ROOT / "weights/phonon_q8_source.safetensors"
    state = {key: value.float() for key, value in load_file(path).items()}
    if len(state) != 185:
        raise ValueError(f"Expected complete pinned 185-tensor checkpoint, got {len(state)}")
    return Phonon(state, flow_capacity).eval()
