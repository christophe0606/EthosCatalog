# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Shared-backbone inference methods for voice/text prefill and audio frames."""
import torch
from torch import nn
from torch.nn import functional as F


class TextEmbedding(nn.Module):
    def __init__(self, model):
        super().__init__()
        self.register_buffer("embedding", model.source.w(
            "flow_lm.condition_provider.conditioners.transcript_in_segment.embed.weight"))

    def forward(self, token):
        return F.embedding(token, self.embedding)


class Synthesize(nn.Module):
    """One 80ms flow/Mimi step, plus the next step's conditioned embedding.

    The next embedding belongs to the newly sampled normalized latent. It is
    consumed on the following backbone call; EOS is tested on the current
    output and the selected post-EOS frames are still decoded as upstream does.
    """
    def __init__(self, model):
        super().__init__()
        self.flow = model.flow
        self.decoder = model.decoder
        self.register_buffer("input_weight", model.source.w("flow_lm.input_linear.weight"))
        self.register_buffer("conditions", model.conditions)

    def forward(self, backbone_output, noise, mimi_cache, mimi_position, audio_states):
        latent, eos = self.flow(backbone_output, noise)
        audio, mimi_cache, audio_states = self.decoder(latent, mimi_cache, mimi_position, audio_states)
        next_embedding = F.linear(latent.reshape(1, 1, 32), self.input_weight) + self.conditions
        return latent, eos, audio, next_embedding, mimi_cache, audio_states
