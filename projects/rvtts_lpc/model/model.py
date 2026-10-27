# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""PyTorch reconstruction of rvTTS's supplied int8-weight inference models.

Original architecture and parameters: Armstrong Subero, Apache-2.0.
See upstream/LICENSE and upstream/README.md. No training is performed here.
"""
from pathlib import Path
import re
import numpy as np
import torch
from torch import nn
import torch.nn.functional as F

HERE = Path(__file__).resolve().parent
UPSTREAM = HERE / "upstream"


class Blob:
    def __init__(self, stem, prefix, filename):
        header = (UPSTREAM / stem).read_text()
        self.meta = dict(re.findall(r"#define\s+" + prefix + r"_(\w+)\s+([^\s/]+)", header))
        self.data = (UPSTREAM / filename).read_bytes()
        assert len(self.data) == int(self.meta["WEIGHT_TOTAL_BYTES"].rstrip("u"))
        assert int.from_bytes(self.data[:4], "little") == int(self.meta["WEIGHT_MAGIC"].rstrip("u"), 0)

    def tensor(self, name, shape=None):
        offset = int(self.meta["OFF_" + name].rstrip("u"))
        count = int(self.meta["COUNT_" + name].rstrip("u"))
        dtype = np.dtype("i1" if self.meta["DTYPE_" + name] == '"int8"' else "<f4")
        array = np.frombuffer(self.data, dtype=dtype, count=count, offset=offset).copy()
        if dtype == np.dtype("i1"):
            array = array.astype(np.float32) * np.float32(self.meta["SCALE_" + name].rstrip("f"))
        return torch.from_numpy(array.reshape(shape) if shape else array)


def linear(blob, w, b, shape):
    layer = nn.Linear(shape[1], shape[0], bias=b is not None)
    layer.weight = nn.Parameter(blob.tensor(w, shape), requires_grad=False)
    if b:
        layer.bias = nn.Parameter(blob.tensor(b), requires_grad=False)
    return layer


def conv(blob, w, b, kernel, dilation=1):
    layer = nn.Conv1d(128, 128, kernel, padding=(kernel // 2) * dilation,
                      dilation=dilation, bias=b is not None)
    layer.weight = nn.Parameter(blob.tensor(w, (128, 128, kernel)), requires_grad=False)
    if b:
        layer.bias = nn.Parameter(blob.tensor(b), requires_grad=False)
    return layer


class Prosody(nn.Module):
    """Complete bidirectional neural model, taking the source's built inputs."""
    def __init__(self):
        super().__init__()
        b = Blob("prosody_weights_int8_meta.h", "PW8", "PROSODY.bin")
        self.pre = linear(b, "PRE_W", "PRE_B", (64, 184))
        self.gru = nn.GRU(64, 64, bidirectional=True, batch_first=True)
        for source, suffix in (("FW", ""), ("BW", "_reverse")):
            for kind, name in (("IH_W", "weight_ih"), ("HH_W", "weight_hh"),
                               ("IH_B", "bias_ih"), ("HH_B", "bias_hh")):
                value = b.tensor("GRU_" + source + "_" + kind)
                if kind.endswith("W"):
                    value = value.reshape(192, 64)
                setattr(self.gru, name + "_l0" + suffix, nn.Parameter(value, requires_grad=False))
        self.mu1 = linear(b, "OUT_MU_W1", "OUT_MU_B1", (64, 128))
        self.mu2 = linear(b, "OUT_MU_W2", "OUT_MU_B2", (3, 64))
        self.lv1 = linear(b, "OUT_LV_W1", "OUT_LV_B1", (64, 128))
        self.lv2 = linear(b, "OUT_LV_W2", "OUT_LV_B2", (3, 64))
        self.register_buffer("mean", b.tensor("NORM_MEAN"))
        self.register_buffer("std", b.tensor("NORM_STD"))

    def forward(self, x):
        hidden, _ = self.gru(torch.tanh(self.pre(x)))
        mu = self.mu2(torch.tanh(self.mu1(hidden)))
        lv = self.lv2(torch.tanh(self.lv1(hidden))).clamp(-6.0, 2.0)
        return mu * self.std + self.mean, lv


class Acoustic(nn.Module):
    """Complete seven-convolution acoustic model, with source input features."""
    def __init__(self):
        super().__init__()
        b = Blob("acoustic_stage2_weights_int8_meta.h", "AS2Q", "ACOUSTIC.bin")
        self.pre = linear(b, "CONV_PRE_W", "CONV_PRE_B", (128, 169))
        self.context = conv(b, "CONTEXT_CONV_W", None, 7)
        self.dilated = nn.ModuleList([conv(b, f"DILATED_{i}_W", f"DILATED_{i}_B", 3, 1 << i)
                                      for i in range(5)])
        self.out1 = linear(b, "OUT_W1", "OUT_B1", (128, 128))
        self.out2 = linear(b, "OUT_W2", "OUT_B2", (17, 128))
        self.register_buffer("mean", b.tensor("NORM_MEAN")[1:18])
        self.register_buffer("std", b.tensor("NORM_STD")[1:18])

    def forward(self, x):
        h = torch.tanh(self.pre(x)).transpose(1, 2)
        h = torch.tanh(self.context(h))
        for layer in self.dilated:
            h = h + torch.tanh(layer(h))
        h = self.out2(torch.tanh(self.out1(h.transpose(1, 2))))
        return h * self.std + self.mean


VOWELS = {2, 3, 4, 5, 6, 7, 12, 13, 14, 18, 19, 26, 27, 34, 35}
FRICATIVES = {15, 36, 30, 39, 31, 40, 33, 11, 17}
STOPS = {8, 10, 16, 28, 32, 21}
AFFRICATES = {9, 20}
NASALS = {23, 24, 25}
LIQUIDS = {22, 29}
GLIDES = {37, 38}
SONORANTS = VOWELS | NASALS | LIQUIDS | GLIDES
VOICED = SONORANTS | {8, 10, 16, 36, 39, 40, 11, 20}


def frame_context(phones):
    """Exact source run boundaries, previous/next distinct phone and auxiliaries."""
    phones = np.asarray(phones)
    n = len(phones)
    pp, np_, aux = [], [], []
    for t, original in enumerate(phones):
        start, end = t, t + 1
        while start and phones[start - 1] == original:
            start -= 1
        while end < n and phones[end] == original:
            end += 1
        cp = int(original) if original < 41 else 0
        prev = int(phones[start - 1]) if start else 0
        next_ = int(phones[end]) if end < n else 0
        pp.append(prev if prev < 41 else 0)
        np_.append(next_ if next_ < 41 else 0)
        length = end - start
        pos = np.float32(t - start) / np.float32(length - 1) if length > 1 else np.float32(0)
        row = [float(cp in group) for group in (VOWELS, FRICATIVES, STOPS, AFFRICATES,
                NASALS, LIQUIDS, GLIDES, SONORANTS, VOICED, {0, 1})]
        row += [pos, np.float32(1) - pos, min(np.float32(length) / np.float32(30), np.float32(2)),
                np.log(np.float32(1 + length)) / np.float32(5)]
        aux.append(row)
    return np.array(pp), np.array(np_), np.array(aux, np.float32)


def build_inputs(phones, stress=None, words=None, conditioning=None):
    """Embedding lookup and deterministic frontend features as in the C source."""
    phones = np.asarray(phones, dtype=np.int64)
    n = len(phones)
    stress = np.zeros(n, np.int64) if stress is None else np.asarray(stress, np.int64)
    words = np.zeros(n, np.int64) if words is None else np.asarray(words, np.int64)
    pp, np_, aux = frame_context(phones)
    cp = np.where(phones < 41, phones, 0)
    stress = np.where(stress < 4, stress, 0)
    words = np.where(words < 14682, words, 0)
    prosody = conditioning is None
    b = (Blob("prosody_weights_int8_meta.h", "PW8", "PROSODY.bin") if prosody else
         Blob("acoustic_stage2_weights_int8_meta.h", "AS2Q", "ACOUSTIC.bin"))
    emb = b.tensor("PHONE_EMB", (41, 48)).numpy()
    stress_emb = b.tensor("STRESS_EMB", (4, 8)).numpy()
    pieces = [emb[pp], emb[cp], emb[np_], stress_emb[stress]]
    if prosody:
        pieces.append(b.tensor("WORD_EMB", (14682, 8)).numpy()[words])
    pieces.append(aux)
    if prosody:
        utt = np.arange(n, dtype=np.float32) / np.float32(max(n - 1, 1))
        sf = np.where(utt > np.float32(.85), (utt - np.float32(.85)) / np.float32(.15), np.float32(0))
        pieces.append(np.stack([utt, 1-utt, sf, np.zeros(n), np.full(n, np.float32(n)*np.float32(.01)/np.float32(30)),
                                (cp > 1).astype(np.float32), np.full(n, .2), utt, aux[:, 10], np.zeros(n)], 1).astype(np.float32))
    else:
        mean = b.tensor("NORM_MEAN").numpy()[[0, 18, 19]]
        std = b.tensor("NORM_STD").numpy()[[0, 18, 19]]
        pieces.append((np.asarray(conditioning, np.float32) - mean) / std)
    return torch.from_numpy(np.concatenate(pieces, 1)).unsqueeze(0)


def sample(seed, frames):
    rng = np.random.default_rng(seed)
    durations = rng.integers(2, 20, size=max(1, frames // 5))
    phones = np.repeat(rng.integers(0, 41, len(durations)), durations)[:frames].astype(np.uint8)
    if len(phones) < frames:
        phones = np.pad(phones, (0, frames-len(phones)))
    stress = rng.integers(0, 4, frames, dtype=np.uint8)
    words = rng.integers(0, 14682, frames, dtype=np.uint16)
    return phones, stress, words


class ProsodyPre(nn.Module):
    def __init__(self, source):
        super().__init__()
        self.pre = source.pre

    def forward(self, x):
        return torch.tanh(self.pre(x))


class ProsodyGRUStep(nn.Module):
    """A paired forward/backward step; CPU owns the exact utterance scan order."""
    def __init__(self, source):
        super().__init__()
        self.ih = nn.ModuleList()
        self.hh = nn.ModuleList()
        for suffix in ("", "_reverse"):
            for layers, weight, bias in ((self.ih, "weight_ih", "bias_ih"), (self.hh, "weight_hh", "bias_hh")):
                layer = nn.Linear(64, 192)
                layer.weight = getattr(source.gru, weight + "_l0" + suffix)
                layer.bias = getattr(source.gru, bias + "_l0" + suffix)
                layers.append(layer)

    def forward(self, x):
        outputs = []
        for i in range(2):
            h = x[:, 128+i*64:192+i*64]
            xi = self.ih[i](x[:, i*64:(i+1)*64])
            hh = self.hh[i](h)
            r = torch.sigmoid(xi[:, :64] + hh[:, :64])
            z = torch.sigmoid(xi[:, 64:128] + hh[:, 64:128])
            candidate = torch.tanh(xi[:, 128:] + r * hh[:, 128:])
            outputs.append((1-z) * candidate + z * h)
        return torch.cat(outputs, 1)


class ProsodyHead(nn.Module):
    def __init__(self, source):
        super().__init__()
        self.mu1, self.mu2 = source.mu1, source.mu2
        self.lv1, self.lv2 = source.lv1, source.lv2
        self.register_buffer("mean", source.mean)
        self.register_buffer("std", source.std)

    def forward(self, x):
        mu = self.mu2(torch.tanh(self.mu1(x))) * self.std + self.mean
        lv = self.lv2(torch.tanh(self.lv1(x))).clamp(-6.0, 2.0)
        return torch.cat((mu, lv), -1)


BATCH = 32
HALO = 34
TILE = BATCH + 2*HALO


class AcousticTile(Acoustic):
    """Exact temporal tile: 34 frames of context cover the receptive field.

    The validity channel zeros every intermediate outside the utterance, as the
    C convolutions skip those frames. It is not ordinary zero input padding.
    """
    def forward(self, x):
        mask = x[:, :, 169:170].transpose(1, 2)
        h = torch.tanh(self.pre(x[:, :, :169])).transpose(1, 2) * mask
        h = torch.tanh(self.context(h)) * mask
        for layer in self.dilated:
            h = (h + torch.tanh(layer(h))) * mask
        h = self.out2(torch.tanh(self.out1(h.transpose(1, 2))))
        return (h * self.std + self.mean)[:, HALO:HALO+BATCH, :]


def method_models():
    p = Prosody().eval()
    return {"prosody_pre": ProsodyPre(p).eval(), "prosody_gru": ProsodyGRUStep(p).eval(),
            "prosody_head": ProsodyHead(p).eval(), "acoustic": AcousticTile().eval()}


def run_methods(models, phones, stress, words, capture=None):
    """Exact original scan and masked-tile orchestration; returns 20 features."""
    def call(name, value):
        if capture is not None:
            capture[name].append(value.detach().clone())
        return models[name](value)
    n = len(phones)
    x = build_inputs(phones, stress, words)[0]
    pre = []
    for start in range(0, n, BATCH):
        block = F.pad(x[start:start+BATCH], (0, 0, 0, max(0, start+BATCH-n)))
        pre.append(call("prosody_pre", block)[0:min(BATCH, n-start)])
    pre = torch.cat(pre)
    fw, bw = torch.zeros(n, 64), torch.zeros(n, 64)
    hf, hb = torch.zeros(1, 64), torch.zeros(1, 64)
    for t in range(n):
        y = call("prosody_gru", torch.cat((pre[t:t+1], pre[n-1-t:n-t], hf, hb), -1))
        hf, hb = y[:, :64], y[:, 64:]
        fw[t], bw[n-1-t] = hf[0], hb[0]
    head = []
    h = torch.cat((fw, bw), 1)
    for start in range(0, n, BATCH):
        block = F.pad(h[start:start+BATCH], (0, 0, 0, max(0, start+BATCH-n)))
        head.append(call("prosody_head", block)[:min(BATCH, n-start)])
    head = torch.cat(head)
    cond = head[:, :3]
    acoustic_input = build_inputs(phones, stress, conditioning=cond.detach().numpy())[0]
    features = torch.zeros(n, 20)
    features[:, 0], features[:, 18], features[:, 19] = cond[:, 0], cond[:, 1], cond[:, 2]
    for start in range(0, n, BATCH):
        tile = torch.zeros(1, TILE, 170)
        lo, hi = max(0, start-HALO), min(n, start+BATCH+HALO)
        offset = lo-start+HALO
        tile[0, offset:offset+hi-lo, :169] = acoustic_input[lo:hi]
        tile[0, offset:offset+hi-lo, 169] = 1
        features[start:min(start+BATCH, n), 1:18] = call("acoustic", tile)[0, :min(BATCH, n-start)]
    return features, head
