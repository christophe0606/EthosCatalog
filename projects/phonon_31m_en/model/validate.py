#!/usr/bin/env python3
# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Source/tensor and streaming-invariance checks; no upstream parity claim."""
import json
from pathlib import Path
import torch
from torch.nn import functional as F
from model import load_model
from quantize import metrics

ROOT = Path(__file__).resolve().parent


def reference_transformer(weights, x, prefix, layers, context=None):
    """Independent nonstreaming formula with complex-valued interleaved RoPE.

    Native PyTorch LayerNorm and SDPA are independent of the export model's
    manual reduction, attention matmuls, mask layout and real-valued RoPE.
    """
    length = x.shape[1]
    frequencies = 10000.0 ** (-torch.arange(32) / 32)
    angles = torch.arange(length)[:, None] * frequencies
    phase = torch.polar(torch.ones_like(angles), angles)[None, None]
    indices = torch.arange(length)
    valid = indices[None, :] <= indices[:, None]
    if context is not None:
        valid &= indices[None, :] >= indices[:, None] - context
    for i in range(layers):
        p = f"{prefix}.layers.{i}"
        normalized = F.layer_norm(x, (512,), weights.w(p + ".norm1.weight"), weights.w(p + ".norm1.bias"), 1e-5)
        q, k, v = F.linear(normalized, weights.w(p + ".self_attn.in_proj.weight")).chunk(3, -1)
        def rotate(tensor):
            pairs = tensor.reshape(1, length, 8, 32, 2).transpose(1, 2).contiguous()
            return torch.view_as_real(torch.view_as_complex(pairs) * phase).flatten(-2)
        q, k = rotate(q), rotate(k)
        v = v.reshape(1, length, 8, 64).transpose(1, 2)
        y = F.scaled_dot_product_attention(q, k, v, attn_mask=valid).transpose(1, 2).reshape(1, length, 512)
        y = F.linear(y, weights.w(p + ".self_attn.out_proj.weight"))
        if context is not None:
            y *= weights.w(p + ".layer_scale_1.scale")
        x = x + y
        normalized = F.layer_norm(x, (512,), weights.w(p + ".norm2.weight"), weights.w(p + ".norm2.bias"), 1e-5)
        y = F.linear(F.gelu(F.linear(normalized, weights.w(p + ".linear1.weight"))), weights.w(p + ".linear2.weight"))
        if context is not None:
            y *= weights.w(p + ".layer_scale_2.scale")
        x = x + y
    return x


def reference_decoder(model, latents):
    """Decode an entire latent sequence with causal padding, without state."""
    w = model.decoder.w
    x = latents.transpose(1, 2) * w("flow_lm.emb_std")[None, :, None] + w("flow_lm.emb_mean")[None, :, None]
    x = F.conv1d(x, w("mimi.quantizer.output_proj.weight"))
    x = F.conv_transpose1d(x, w("mimi.upsample.convtr.convtr.weight"), stride=16, groups=512)[..., :-16]
    x = reference_transformer(model.decoder.backbone, x.transpose(1, 2),
                              "mimi.decoder_transformer.transformer", 2, 250).transpose(1, 2)
    def conv(value, name):
        weight = w(name + ".weight")
        return F.conv1d(F.pad(value, (weight.shape[-1] - 1, 0)), weight, w(name + ".bias"))
    x = conv(x, "mimi.decoder.model.0.conv")
    for up, res, stride in ((2, 3, 6), (5, 6, 5), (8, 9, 4)):
        p = f"mimi.decoder.model.{up}.convtr"
        x = F.conv_transpose1d(F.elu(x), w(p + ".weight"), w(p + ".bias"), stride=stride)[..., :-stride]
        h = conv(F.elu(x), f"mimi.decoder.model.{res}.block.1.conv")
        h = conv(F.elu(h), f"mimi.decoder.model.{res}.block.3.conv")
        x = x + h
    return conv(F.elu(x), "mimi.decoder.model.11.conv")


def main():
    torch.set_num_threads(4)
    torch.manual_seed(314159)
    model = load_model()
    report = {"upstream_runtime_parity": False,
              "limitations": "Independent source-formula/state tests do not prove upstream waveform parity or perceptual quality."}
    with torch.no_grad():
        from model import silu
        activation = torch.linspace(-20, 20, 4097)
        report["silu_portable_formula"] = metrics(silu(activation), F.silu(activation))
        torch.testing.assert_close(silu(activation), F.silu(activation), atol=2e-6, rtol=1e-6)
        x = torch.randn(1, 20, 512)
        cache = model.initial_state()[0]
        ys = []
        for i in range(x.shape[1]):
            y, cache = model.backbone(x[:, i:i + 1], cache, torch.tensor(float(i)))
            ys.append(y)
        reference = reference_transformer(model.backbone, x, "flow_lm.transformer", 6)
        report["flow_streamed_vs_independent_batch"] = metrics(torch.cat(ys, 1), reference)
        torch.testing.assert_close(torch.cat(ys, 1), reference, atol=0.0001, rtol=0.0001)
        # 18 frames cross Mimi's 250-position attention window at frame 16.
        latents = torch.randn(1, 18, 32)
        _, cache, states = model.initial_state()
        audio = []
        for i in range(latents.shape[1]):
            chunk, cache, states = model.decoder(latents[:, i], cache, torch.tensor(float(i * 16)), states)
            audio.append(chunk)
        reference = reference_decoder(model, latents)
        report["mimi_streamed_vs_independent_offline"] = metrics(torch.cat(audio, -1), reference)
        torch.testing.assert_close(torch.cat(audio, -1), reference, atol=0.0001, rtol=0.0001)
    import sentencepiece as spm
    from tokenizers import Tokenizer
    sp = spm.SentencePieceProcessor(model_file=str(ROOT / "weights/tokenizer.model"))
    hf = Tokenizer.from_file(str(ROOT / "weights/tokenizer.json"))
    texts = ["Hello from Phonon.", "The quick brown fox jumps over the lazy dog.", "One, two, three!", "Could you read this?"]
    report["tokenizer_parity"] = [{"text": text, "equal": sp.encode(text) == hf.encode(text).ids,
                                  "tokens": sp.encode(text)} for text in texts]
    if not all(row["equal"] for row in report["tokenizer_parity"]):
        raise ValueError("Published tokenizer representations disagree")
    report["status"] = "source_formula_checks_pass"
    (ROOT / "validation_metrics.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
