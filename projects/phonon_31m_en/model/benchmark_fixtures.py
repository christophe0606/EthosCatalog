#!/usr/bin/env python3
# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Generate deterministic target checks from the final saved PTQ methods.

These fixtures check target execution against the saved pre-Vela PTQ graph.
They do not establish upstream parity, speech quality or hardware performance.
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import re
import sys

import numpy as np
import sentencepiece as spm
import torch

LOCAL = Path(__file__).resolve().parent
PROJECT = LOCAL.parent
ROOT = PROJECT.parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from quantized_model import load_quantized_program

SEED = 20261021
NOISE_FRAMES = 64
REFERENCE_FRAMES = 2
TEXTS = ("Hello from Phonon.", "The quick brown fox jumps over the lazy dog.")


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def normalize(text):
    text = re.sub(r"\s+", " ", text.replace("\r", " ").replace("\n", " ").strip())
    if not text:
        raise ValueError("Empty benchmark text")
    text = text[0].upper() + text[1:]
    return text + "." if text[-1].isalnum() else text


def floats(values):
    """Nine significant digits preserve each finite IEEE float32 value."""
    array = np.asarray(values, dtype=np.float32).reshape(-1)
    if not np.isfinite(array).all():
        raise ValueError("Nonfinite benchmark fixture")
    parts = []
    for value in array:
        number = format(float(value), ".9g")
        if "." not in number and "e" not in number.lower():
            number += ".0"
        parts.append(number + "f")
    return ",\n".join("    " + ", ".join(parts[i:i + 8]) for i in range(0, len(parts), 8))


def main():
    torch.set_num_threads(4)
    manifest_path = LOCAL / "method_manifest.json"
    manifest = json.loads(manifest_path.read_text())
    methods = {name: load_quantized_program(manifest["target"], PROJECT, settings).module(check_guards=False)
               for name, settings in manifest["methods"].items()}
    for settings in manifest["methods"].values():
        for key in ("graph", "safetensors"):
            if sha256(PROJECT / settings[key]) != settings[key + "_sha256"]:
                raise ValueError("Method cache differs from conversion manifest")
    startup = manifest["startup"]
    prefix_path = PROJECT / startup["voice_prefix"]["raw_file"]
    bos_path = PROJECT / startup["bos_raw"]["file"]
    if (sha256(prefix_path) != startup["voice_prefix"]["raw_sha256"]
            or sha256(bos_path) != startup["bos_raw"]["sha256"]):
        raise ValueError("Startup asset differs from conversion manifest")
    tokenizer_path = LOCAL / "weights/tokenizer.model"
    tokenizer = spm.SentencePieceProcessor(model_file=str(tokenizer_path))
    prefix = torch.from_numpy(np.frombuffer(prefix_path.read_bytes(), dtype="<f4").copy()).reshape(
        manifest["state_shapes"]["flow_kv"])
    bos = torch.from_numpy(np.frombuffer(bos_path.read_bytes(), dtype="<f4").copy()).reshape(1, 1, 512)
    generator = torch.Generator().manual_seed(SEED)
    noise = torch.stack([torch.randn(1, 32, generator=generator) * (0.8 ** 0.5)
                         for _ in range(NOISE_FRAMES)])
    rows = []
    with torch.no_grad():
        for raw_text in TEXTS:
            text = normalize(raw_text)
            tokens = tokenizer.encode(text)
            flow_cache = prefix.clone()
            position = startup["voice_prefix"]["position"]
            if position + len(tokens) + NOISE_FRAMES > manifest["flow_capacity"]:
                raise ValueError("Fixture exceeds model context capacity")
            mimi_cache = torch.zeros(manifest["state_shapes"]["mimi_kv"])
            conv_states = tuple(torch.zeros(shape) for shape in manifest["state_shapes"]["convolution"])
            for token in tokens:
                embedding = methods["text_embedding"](torch.tensor([[token]], dtype=torch.int32))
                _, flow_cache = methods["backbone"](embedding, flow_cache, torch.tensor(float(position)))
                position += 1
            row = dict(text=text, token_ids=tokens, flow_position_after_text=position,
                       post_eos_frames=3 if len(text.split()) <= 4 else 1, reference=[])
            embedding = bos.clone()
            for frame in range(REFERENCE_FRAMES):
                backbone, flow_cache = methods["backbone"](embedding, flow_cache, torch.tensor(float(position)))
                latent, eos, pcm, embedding, mimi_cache, conv_states = methods["synthesize"](
                    backbone, noise[frame], mimi_cache, torch.tensor(float(frame * 16)), conv_states)
                row["reference"].append(dict(latent=latent.flatten().tolist(), eos=float(eos.item()),
                    pcm=pcm.flatten().tolist(), backbone_output=backbone.flatten().tolist()))
                position += 1
            rows.append(row)
            print(f"[fixtures] {text}: {len(tokens)} tokens, {REFERENCE_FRAMES} reference frames", flush=True)
    provenance = dict(method_manifest_sha256=sha256(manifest_path),
                      methods=manifest["methods"], tokenizer_sha256=sha256(tokenizer_path),
                      prefix_sha256=sha256(prefix_path), bos_sha256=sha256(bos_path))
    report = dict(format="phonon-target-fixtures-v1", seed=SEED, temperature=0.8,
                  noise_frames=NOISE_FRAMES, reference_frames=REFERENCE_FRAMES,
                  noise_shape=[NOISE_FRAMES, 32], noise=noise.reshape(NOISE_FRAMES, 32).tolist(),
                  sample_rate=24000, pcm_samples_per_frame=1920,
                  quality_accepted=False, reference="saved pre-Vela PTQ graphs with final PTQ voice prefix",
                  provenance=provenance, utterances=rows)
    (LOCAL / "benchmark_fixtures.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    lines = ["// Generated by model/benchmark_fixtures.py; do not edit.",
             "// Copyright 2026 Arm Limited and/or its affiliates.", "// SPDX-License-Identifier: Apache-2.0",
             "#pragma once", "#include <stdint.h>",
             f"#define PHONON_BENCHMARK_FIXTURE_COUNT {len(rows)}U",
             f"#define PHONON_BENCHMARK_NOISE_FRAMES {NOISE_FRAMES}U",
             f"#define PHONON_BENCHMARK_REFERENCE_FRAMES {REFERENCE_FRAMES}U",
             "static const float phonon_benchmark_noise[64][32] = {", floats(noise.numpy()), "};",
             "typedef struct {", "  const char *text;", "  const int32_t *tokens;", "  uint32_t token_count;",
             "  uint32_t post_eos_frames;", "  const float *expected_latent;", "  const float *expected_eos;",
             "  const float *expected_pcm;", "  const float *expected_backbone;", "} PhononBenchmarkFixture;"]
    for index, row in enumerate(rows):
        lines += [f"static const int32_t phonon_benchmark_tokens_{index}[] = {{" +
                  ", ".join(map(str, row["token_ids"])) + "};"]
        for key, width in (("latent", 32), ("eos", 1), ("pcm", 1920), ("backbone_output", 512)):
            values = np.asarray([frame[key] for frame in row["reference"]], dtype=np.float32)
            lines += [f"static const float phonon_benchmark_{key}_{index}[{REFERENCE_FRAMES * width}] = {{",
                      floats(values), "};"]
    lines += ["static const PhononBenchmarkFixture phonon_benchmark_fixtures[] = {"]
    for index, row in enumerate(rows):
        lines += [f'  {{{json.dumps(row["text"])}, phonon_benchmark_tokens_{index}, {len(row["token_ids"])}U, '
                  f'{row["post_eos_frames"]}U, phonon_benchmark_latent_{index}, phonon_benchmark_eos_{index}, '
                  f'phonon_benchmark_pcm_{index}, phonon_benchmark_backbone_output_{index}}},']
    lines += ["};", "namespace phonon_fixture {",
              f"constexpr unsigned kTextCount = {len(rows)};",
              f"constexpr unsigned kFrames = {NOISE_FRAMES};",
              f"constexpr unsigned kReferenceFrames = {REFERENCE_FRAMES};",
              "struct Case {", "  const char *text;", "  const int32_t *tokens;",
              "  unsigned token_count, word_count;", "  const float *noise;",
              "  const float *expected_latent;", "  const float *expected_eos;",
              "  const float *expected_pcm;", "};", "static const Case cases[] = {"]
    for index, row in enumerate(rows):
        lines += [f'  {{{json.dumps(row["text"])}, phonon_benchmark_tokens_{index}, {len(row["token_ids"])}U, '
                  f'{len(row["text"].split())}U, &phonon_benchmark_noise[0][0], '
                  f'phonon_benchmark_latent_{index}, phonon_benchmark_eos_{index}, phonon_benchmark_pcm_{index}}},']
    lines += ["};", "} // namespace phonon_fixture", ""]
    (LOCAL / "benchmark_fixtures.h").write_text("\n".join(lines), encoding="utf-8")


if __name__ == "__main__":
    main()
