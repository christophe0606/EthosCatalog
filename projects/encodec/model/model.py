# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Streaming inference for the upstream EnCodec 24 kHz mono checkpoint."""

import hashlib
from pathlib import Path

import torch
from torch import nn
from torch.nn import functional as F

SAMPLE_RATE = 24_000
HOP = 320
FRAMES = 9
BLOCK_SAMPLES = HOP * FRAMES
BOOKS = 8  # 6 kb/s at 75 frames/s and ten bits per code
CHECKPOINT_SHA256 = "d7cc33bcf1aad7f2dad9836f36431530744abeace3ca033005e3290ed4fa47bf"


class StreamState:
    """Read state in layer order and pack the updated state in the same order."""

    def __init__(self, packed):
        self.packed = packed
        self.offset = 0
        self.updated = []

    def read(self, shape):
        count = 1
        for size in shape:
            count *= size
        value = self.packed[self.offset:self.offset + count].reshape(shape)
        self.offset += count
        return value

    def write(self, value):
        self.updated.append(value.flatten())

    def finish(self):
        return torch.cat(self.updated)


def parameter(value):
    return nn.Parameter(value.contiguous(), requires_grad=False)


def load_convolution(weights, name):
    weight = torch._weight_norm(weights[name + ".weight_v"], weights[name + ".weight_g"], 0)
    return weight, weights[name + ".bias"]


class CausalConv(nn.Module):
    def __init__(self, weights, name, stride=1):
        super().__init__()
        weight, bias = load_convolution(weights, name + ".conv.conv")
        self.weight = parameter(weight)
        self.bias = parameter(bias)
        self.stride = stride
        self.history = weight.shape[-1] - stride
        self.state_shape = (1, weight.shape[1], self.history)
        self.state_size = weight.shape[1] * self.history

    def forward(self, x, state, reset):
        if self.history:
            previous = state.read(self.state_shape)
            # The pretrained model uses reflection only at the start of a clip.
            initial = x[..., 1:self.history + 1].flip(-1)
            x = torch.cat((torch.where(reset, initial, previous), x), dim=-1)
            state.write(x[..., -self.history:])
        return F.conv1d(x, self.weight, self.bias, stride=self.stride)


class CausalUpsample(nn.Module):
    """Evaluate a kernel-2s, stride-s transposed convolution as s output phases."""

    def __init__(self, weights, name, stride):
        super().__init__()
        weight, bias = load_convolution(weights, name + ".convtr.convtr")
        channels, self.outputs, kernel = weight.shape
        if kernel != 2 * stride:
            raise ValueError("Expected the EnCodec upsampling kernel to span two input frames")
        # Output phase p receives taps p+s from the previous input and p from
        # the current input. Ordinary convolution therefore reads two frames.
        phases = weight.reshape(channels, self.outputs, 2, stride).permute(1, 3, 0, 2)
        self.weight = parameter(phases.flip(-1).reshape(self.outputs * stride, channels, 2))
        self.bias = parameter(bias.repeat_interleave(stride))
        self.stride = stride
        self.state_shape = (1, channels, 1)
        self.state_size = channels

    def forward(self, x, state, reset):
        previous = state.read(self.state_shape)
        previous = torch.where(reset, torch.zeros_like(previous), previous)
        state.write(x[..., -1:])
        phases = F.conv1d(torch.cat((previous, x), dim=-1), self.weight, self.bias)
        return phases.reshape(1, self.outputs, self.stride, -1).transpose(2, 3).flatten(2)


class Residual(nn.Module):
    def __init__(self, weights, name):
        super().__init__()
        self.first = CausalConv(weights, name + ".block.1")
        self.second = CausalConv(weights, name + ".block.3")
        self.shortcut = CausalConv(weights, name + ".shortcut")
        self.state_size = self.first.state_size

    def forward(self, x, state, reset):
        residual = self.first(F.elu(x), state, reset)
        residual = self.second(F.elu(residual), state, reset)
        return self.shortcut(x, state, reset) + residual


class RecurrentLayer(nn.Module):
    def __init__(self, weights, name, layer):
        super().__init__()
        suffix = f"_l{layer}"
        self.input_matrix = parameter(weights[name + ".weight_ih" + suffix])
        self.hidden_matrix = parameter(weights[name + ".weight_hh" + suffix])
        self.input_bias = parameter(weights[name + ".bias_ih" + suffix])
        self.hidden_bias = parameter(weights[name + ".bias_hh" + suffix])
        self.width = self.hidden_matrix.shape[1]
        self.state_size = 2 * self.width

    def step(self, projected, hidden, cell):
        gates = projected + F.linear(hidden, self.hidden_matrix, self.hidden_bias)
        update, forget, proposal, expose = gates.chunk(4, dim=-1)
        cell = forget.sigmoid() * cell + update.sigmoid() * proposal.tanh()
        return expose.sigmoid() * cell.tanh(), cell

    def forward(self, x, state, reset):
        hidden = state.read((1, self.width))
        cell = state.read((1, self.width))
        hidden = torch.where(reset, torch.zeros_like(hidden), hidden)
        cell = torch.where(reset, torch.zeros_like(cell), cell)
        projected = F.linear(x, self.input_matrix, self.input_bias)
        outputs = []
        for frame in range(x.shape[1]):
            hidden, cell = self.step(projected[:, frame], hidden, cell)
            outputs.append(hidden)
        state.write(hidden)
        state.write(cell)
        return torch.stack(outputs, dim=1)


class Recurrent(nn.Module):
    def __init__(self, weights, name):
        super().__init__()
        self.layers = nn.ModuleList(RecurrentLayer(weights, name + ".lstm", i) for i in range(2))
        self.state_size = sum(layer.state_size for layer in self.layers)

    def forward(self, x, state, reset):
        y = x.transpose(1, 2)
        for layer in self.layers:
            y = layer(y, state, reset)
        return y.transpose(1, 2) + x


class Codebook(nn.Module):
    def __init__(self, table):
        super().__init__()
        self.table = parameter(table)
        self.register_buffer("norm", table.square().sum(dim=1))

    def encode(self, residual):
        # Keep the distance order used by the upstream residual quantizer.
        distance = residual.square().sum(dim=-1, keepdim=True) - 2 * F.linear(residual, self.table)
        return (-(distance + self.norm)).argmax(dim=-1).to(torch.int32)

    def decode(self, codes):
        return F.embedding(codes, self.table)


def codebooks(weights, count):
    return nn.ModuleList(Codebook(weights[f"quantizer.vq.layers.{i}._codebook.embed"]) for i in range(count))


class Encoder(nn.Module):
    def __init__(self, weights, books):
        super().__init__()
        name = "encoder.model"
        self.head = CausalConv(weights, name + ".0")
        self.residuals = nn.ModuleList(Residual(weights, f"{name}.{3 * i + 1}") for i in range(4))
        self.down = nn.ModuleList(
            CausalConv(weights, f"{name}.{3 * i + 3}", stride) for i, stride in enumerate((2, 4, 5, 8))
        )
        self.recurrent = Recurrent(weights, name + ".13")
        self.tail = CausalConv(weights, name + ".15")
        self.books = codebooks(weights, books)
        self.state_size = sum(layer.state_size for layer in
                              (self.head, *self.residuals, *self.down, self.recurrent, self.tail))

    def analyze(self, audio, state, reset):
        x = self.head(audio, state, reset)
        for residual, down in zip(self.residuals, self.down):
            x = down(F.elu(residual(x, state, reset)), state, reset)
        return x.transpose(1, 2)

    def latent(self, audio, previous, reset):
        state = StreamState(previous)
        x = self.analyze(audio, state, reset).transpose(1, 2)
        x = self.recurrent(x, state, reset)
        return self.tail(F.elu(x), state, reset).transpose(1, 2), state.finish()

    def encode_latent(self, residual):
        codes = []
        for book in self.books:
            code = book.encode(residual)
            codes.append(code)
            residual = residual - book.decode(code)
        return torch.stack(codes, dim=1)

    def forward(self, audio, previous, reset):
        residual, state = self.latent(audio, previous, reset)
        return self.encode_latent(residual), state


class Decoder(nn.Module):
    def __init__(self, weights, books):
        super().__init__()
        name = "decoder.model"
        self.books = codebooks(weights, books)
        self.head = CausalConv(weights, name + ".0")
        self.recurrent = Recurrent(weights, name + ".1")
        self.up = nn.ModuleList(
            CausalUpsample(weights, f"{name}.{3 * i + 3}", stride) for i, stride in enumerate((8, 5, 4, 2))
        )
        self.residuals = nn.ModuleList(Residual(weights, f"{name}.{3 * i + 4}") for i in range(4))
        self.tail = CausalConv(weights, name + ".15")
        self.state_size = sum(layer.state_size for layer in
                              (self.head, self.recurrent, *self.up, *self.residuals, self.tail))

    def reconstruct(self, x, state, reset):
        for up, residual in zip(self.up, self.residuals):
            x = residual(up(F.elu(x), state, reset), state, reset)
        return self.tail(F.elu(x), state, reset)

    def synthesize(self, latent, previous, reset):
        state = StreamState(previous)
        x = self.head(latent, state, reset)
        x = self.recurrent(x, state, reset)
        return self.reconstruct(x, state, reset), state.finish()

    def embed(self, codes):
        latent = self.books[0].decode(codes[:, 0])
        for index, book in enumerate(self.books[1:], start=1):
            latent = latent + book.decode(codes[:, index])
        return latent.transpose(1, 2)

    def forward(self, codes, previous, reset):
        return self.synthesize(self.embed(codes), previous, reset)


class Front(nn.Module):
    """Compute analysis features and project the first layer's input for the block."""

    def __init__(self, codec):
        super().__init__()
        self.codec = codec
        self.encode = isinstance(codec, Encoder)
        self.state_size = (codec.state_size - codec.recurrent.state_size - codec.tail.state_size
                           if self.encode else codec.head.state_size)

    def forward(self, data, previous, reset):
        state = StreamState(previous)
        if self.encode:
            features = self.codec.analyze(data, state, reset)
        else:
            features = self.codec.head(self.codec.embed(data), state, reset).transpose(1, 2)
        layer = self.codec.recurrent.layers[0]
        projected = F.linear(features, layer.input_matrix, layer.input_bias)
        return features, projected, state.finish()


class HiddenStep(nn.Module):
    """Keep the recurrent matrix in one delegate reused at every latent frame."""

    def __init__(self, layer):
        super().__init__()
        self.layer = layer

    def forward(self, projected, previous, reset):
        hidden = previous[:self.layer.width].unsqueeze(0)
        cell = previous[self.layer.width:].unsqueeze(0)
        hidden = torch.where(reset, torch.zeros_like(hidden), hidden)
        cell = torch.where(reset, torch.zeros_like(cell), cell)
        hidden, cell = self.layer.step(projected, hidden, cell)
        return hidden, torch.cat((hidden.flatten(), cell.flatten()))


class InputProjection(nn.Module):
    def __init__(self, layer):
        super().__init__()
        self.weight = layer.input_matrix
        self.bias = layer.input_bias

    def forward(self, features):
        return F.linear(features, self.weight, self.bias)


class Back(nn.Module):
    def __init__(self, codec):
        super().__init__()
        self.codec = codec
        self.encode = isinstance(codec, Encoder)
        self.state_size = (codec.tail.state_size if self.encode else
                           codec.state_size - codec.recurrent.state_size - codec.head.state_size)

    def forward(self, features, recurrent, previous, reset):
        state = StreamState(previous)
        x = (features + recurrent).transpose(1, 2)
        if self.encode:
            latent = self.codec.tail(F.elu(x), state, reset).transpose(1, 2)
            output = self.codec.encode_latent(latent)
        else:
            output = self.codec.reconstruct(x, state, reset)
        return output, state.finish()


def staged_methods(encoder, decoder):
    methods = {}
    for name, codec in (("encode", encoder), ("decode", decoder)):
        first, second = codec.recurrent.layers
        methods.update({
            name + "_front": Front(codec),
            name + "_hidden0": HiddenStep(first),
            name + "_project1": InputProjection(second),
            name + "_hidden1": HiddenStep(second),
            name + "_back": Back(codec),
        })
    return methods


def run_stages(call, name, data, state, reset):
    """Compose the stage calls in runtime order using four independent states."""
    features, projection, front = call(name + "_front", data, state[0], reset)
    later = torch.zeros_like(reset)
    states = [front]
    for layer in range(2):
        hidden_state = state[layer + 1]
        frames = []
        for index, affine in enumerate(projection.unbind(dim=1)):
            hidden, hidden_state = call(name + f"_hidden{layer}", affine, hidden_state,
                                        reset if index == 0 else later)
            frames.append(hidden)
        recurrent = torch.stack(frames, dim=1)
        states.append(hidden_state)
        if layer == 0:
            projection = call(name + "_project1", recurrent)
    output, back = call(name + "_back", features, recurrent, state[3], reset)
    return output, (*states, back)


def read_weights(checkpoint):
    checkpoint = Path(checkpoint)
    with checkpoint.open("rb") as stream:
        actual = hashlib.file_digest(stream, "sha256").hexdigest()
    if actual != CHECKPOINT_SHA256:
        raise ValueError(f"Unexpected hash for the EnCodec 24 kHz checkpoint: {actual}")
    return torch.load(checkpoint, map_location="cpu", weights_only=True)


def load_codec(checkpoint, books=BOOKS):
    if books not in (2, 4, 8, 16, 32):
        raise ValueError("The pretrained model supports 2, 4, 8, 16 or 32 codebooks")
    weights = read_weights(checkpoint)
    return Encoder(weights, books).eval(), Decoder(weights, books).eval()
