#!/usr/bin/env python3
# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Measure selected upstream q8 release against same-revision trained floats.

This reports numerical errors, without claiming a perceptual quality score.
The reference is validation-only and never replaces q8 production weights.
"""
import json
import argparse
from pathlib import Path
import torch
from safetensors.torch import load_file
from import_q8 import sha256, download
from model import load_model
from quantize import inputs, metrics

ROOT = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", type=Path, default=ROOT / "weights/float_reference.safetensors")
    parser.add_argument("--download-reference", action="store_true")
    args = parser.parse_args()
    torch.set_num_threads(4)
    original_path = args.reference
    if args.download_reference:
        download("model.safetensors", original_path)
    if sha256(original_path) != "8a04cf833cfe69b67c2ec968b2d31244655b9accc84ade9c0ebcc176f2074607":
        raise ValueError("Reference differs from the exact pinned original weights")
    q8_path = ROOT / "weights/phonon_q8_source.safetensors"
    original, q8 = load_file(original_path), load_file(q8_path)
    if set(original) != set(q8):
        raise ValueError("Original and q8 tensor coverage differs")
    tensor_errors = {key: metrics(q8[key], original[key]) for key in original}
    quantized, floating = load_model(), load_model(original_path)
    texts = ["Hello from Phonon.", "The quick brown fox jumps over the lazy dog."]
    frame_errors = []
    with torch.no_grad():
        for sample in inputs(quantized, texts, frames=8):
            q = quantized(*sample)
            f = floating(*sample)
            frame_errors.append({name: metrics(a, b) for name, a, b in zip(
                ("latent", "eos_logit", "pcm"), q[:3], f[:3])})
    report = dict(hf_revision="52433c46975bfa13144c56204c9f4ee1c5b148aa",
                  original_sha256=sha256(original_path), q8_import_sha256=sha256(q8_path),
                  compared_tensors=len(tensor_errors), tensor_errors=tensor_errors,
                  texts=texts, frame_count=len(frame_errors), frame_errors=frame_errors,
                  conditions="Teacher-forced: identical previous latent, Gaussian noise, q8-generated KV/convolution states and default voice per frame;8frames/text; temp0.8; seed20261021.",
                  limitations="Numerical error on generated early frames only; no WER, listening test, upstream executable parity or perceptual acceptance claim.")
    (ROOT / "source_quantization_metrics.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"compared_tensors": len(tensor_errors), "frames": len(frame_errors),
          "max_error": {name: max(row[name]["max_absolute_error"] for row in frame_errors)
                        for name in ("latent", "eos_logit", "pcm")}}, indent=2))


if __name__ == "__main__":
    main()
