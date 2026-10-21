#!/usr/bin/env python3
# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Derive deterministic default-voice startup state from the final PTQ backbone."""
import json
from pathlib import Path
import torch
import torchao.quantization.pt2e.quantize_pt2e  # register decomposed operators
from safetensors.torch import load_file, save_file
from import_q8 import sha256

LOCAL = Path(__file__).resolve().parent


def create_voice_prefix(backbone, voice, bos, manifest):
    cache = torch.zeros(manifest["state_shapes"]["flow_kv"])
    with torch.no_grad():
        for i in range(voice.shape[1]):
            _, cache = backbone(voice[:, i:i + 1], cache, torch.tensor(float(i)))
    cache = cache.detach().contiguous()
    provenance = dict(source_gguf_sha256="a7d9aad2cd6dc66473f24a08bc6d2939030ef4d751ef82349361fbef3272dbae",
                      backbone_graph_sha256=manifest["methods"]["backbone"]["graph_sha256"],
                      backbone_safetensors_sha256=manifest["methods"]["backbone"]["safetensors_sha256"],
                      default_voice_sha256=sha256(LOCAL / "weights/default-voice.safetensors"),
                      position=str(voice.shape[1]), license="Apache-2.0")
    safetensors = LOCAL / "weights/voice_prefix.safetensors"
    save_file(dict(flow_cache=cache), safetensors, metadata=provenance)
    prefix = LOCAL / "weights/voice_prefix.bin"
    prefix.write_bytes(cache.numpy().astype("<f4").tobytes())
    bos_path = LOCAL / "weights/bos_embedding.bin"
    bos_path.write_bytes(bos.detach().contiguous().numpy().astype("<f4").tobytes())
    # Validate storage and the next text-token step against explicit prefill.
    restored = load_file(safetensors)["flow_cache"]
    raw = torch.frombuffer(bytearray(prefix.read_bytes()), dtype=torch.float32).reshape(cache.shape)
    if not torch.equal(restored, cache) or not torch.equal(raw, cache):
        raise ValueError("Default voice prefix changed during serialization")
    text = torch.export.load(LOCAL / "text_embedding_quantized.pt2").module(check_guards=False)
    with torch.no_grad():
        emb = text(torch.tensor([[3248]], dtype=torch.int32))
        expected = backbone(emb, cache, torch.tensor(float(voice.shape[1])))
        actual = backbone(emb, raw, torch.tensor(float(voice.shape[1])))
    if not all(torch.equal(a, b) for a, b in zip(expected, actual)):
        raise ValueError("Stored prefix changes subsequent text prompt outputs")
    return dict(voice_prefix=dict(file="model/weights/voice_prefix.safetensors", sha256=sha256(safetensors),
                raw_file="model/weights/voice_prefix.bin", raw_bytes=prefix.stat().st_size,
                raw_sha256=sha256(prefix), shape=list(cache.shape), dtype="float32_little_endian",
                position=int(voice.shape[1]), provenance=provenance,
                validation="Bit-exact safetensors/raw roundtrip and next text-token output/cache vs explicit PTQ voice prefill"),
                bos_raw=dict(file="model/weights/bos_embedding.bin", bytes=bos_path.stat().st_size,
                             sha256=sha256(bos_path), shape=list(bos.shape), dtype="float32_little_endian"))


def main():
    torch.set_num_threads(4)
    path = LOCAL / "method_manifest.json"
    manifest = json.loads(path.read_text())
    source = load_file(LOCAL / "weights/startup.safetensors")
    backbone = torch.export.load(LOCAL / "backbone_quantized.pt2").module(check_guards=False)
    manifest["startup"].update(create_voice_prefix(backbone, source["default_voice_embeddings"], source["bos_embedding"], manifest))
    path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(manifest["startup"], indent=2))


if __name__ == "__main__":
    main()
