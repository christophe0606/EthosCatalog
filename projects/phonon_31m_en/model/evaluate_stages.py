#!/usr/bin/env python3
# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Evaluate complete q8/PTQ utterances on text/noise held out from calibration."""
import json
import torch
import torchao.quantization.pt2e.quantize_pt2e  # register saved decomposed ops
from safetensors.torch import load_file
from model import load_model
from stages import TextEmbedding, Synthesize
from export_stages import LOCAL, corpus, write_wavs


def main():
    torch.set_num_threads(4)
    source = load_model()
    names = ("backbone", "synthesize", "text_embedding")
    quantized = {name: torch.export.load(LOCAL / f"{name}_quantized.pt2").module(check_guards=False) for name in names}
    text = ["One, two, three!", "Could you read this?", "This is a real time speech synthesis test."]
    seed = 314159
    with torch.no_grad():
        voice = load_file(LOCAL / "weights/default-voice.safetensors")["speaker_wavs"].transpose(1, 2)
        voice = source.source.linear(voice, "flow_lm.condition_provider.conditioners.speaker_wavs.output_proj")
        _, _, original = corpus(source.backbone, Synthesize(source).eval(), TextEmbedding(source).eval(),
                                 source, voice, text, seed, 96, collect=False)
        _, _, ptq = corpus(quantized["backbone"], quantized["synthesize"], quantized["text_embedding"],
                            source, voice, text, seed, 96, collect=False)
    report = dict(quality_accepted=False, hardware_realtime_verified=False, seed=seed, temperature=0.8,
                  purpose="held-out text and noise; no observer updates or PTQ recalibration",
                  source_utterances=write_wavs("heldout_q8", original),
                  ptq_free_recurrence_utterances=write_wavs("heldout_ptq", ptq))
    (LOCAL / "heldout_metrics.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"source": [(x["frames"], x["eos_frame"]) for x in original],
                      "ptq": [(x["frames"], x["eos_frame"]) for x in ptq]}, indent=2))


if __name__ == "__main__":
    main()
