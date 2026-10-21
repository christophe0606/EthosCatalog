#!/usr/bin/env python3
# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Import the pinned trained q8 GGUF without a GGUF/GPL dependency.

Only BF16 and GGML Q8_0 are accepted. This is a tensor/container conversion,
not an assertion of end-to-end TTS parity or an Ethos quantized export.
"""
from __future__ import annotations

import argparse
import collections
import hashlib
import json
import math
from pathlib import Path
import struct
import urllib.request

HF_REVISION = "52433c46975bfa13144c56204c9f4ee1c5b148aa"
RUNTIME_REVISION = "f07d7952db0c6127d5e4bc3f41c82b3304b6b5aa"
ROOT = Path(__file__).resolve().parent
BASE_URL = f"https://huggingface.co/gradium/phonon-31m-en/resolve/{HF_REVISION}"
Q8_SHA256 = "a7d9aad2cd6dc66473f24a08bc6d2939030ef4d751ef82349361fbef3272dbae"


class GGUF:
    """Strict little-endian GGUF v3 reader for this pinned q8 release."""

    def __init__(self, path):
        self.path = Path(path)
        with self.path.open("rb") as f:
            self._file = f
            if f.read(4) != b"GGUF" or self._read("I") != 3:
                raise ValueError("Expected GGUF v3")
            tensor_count, metadata_count = self._read("Q"), self._read("Q")
            self.metadata = {}
            for _ in range(metadata_count):
                key = self._string()
                self.metadata[key] = self._value(self._read("I"))
            self.tensors = []
            names = set()
            for _ in range(tensor_count):
                name = self._string()
                if name in names:
                    raise ValueError(f"Duplicate GGUF tensor: {name}")
                names.add(name)
                shape = [self._read("Q") for _ in range(self._read("I"))][::-1]
                typ, offset = self._read("I"), self._read("Q")
                count = math.prod(shape)
                if typ == 30:  # GGML_TYPE_BF16, NOT float32.
                    nbytes = count * 2
                elif typ == 8 and count % 32 == 0:  # GGML_TYPE_Q8_0.
                    nbytes = count // 32 * 34
                else:
                    raise ValueError(f"Unsupported tensor {name}: GGML type {typ}")
                self.tensors.append(dict(name=name, shape=shape, ggml_type=typ,
                                         offset=offset, elements=count, nbytes=nbytes))
            alignment = self.metadata.get("general.alignment", 32)
            self.data_offset = (f.tell() + alignment - 1) // alignment * alignment
        self._file = None
        size = self.path.stat().st_size
        for tensor in self.tensors:
            if self.data_offset + tensor["offset"] + tensor["nbytes"] > size:
                raise ValueError(f"Truncated tensor {tensor['name']}")

    def _read(self, fmt):
        return struct.unpack("<" + fmt, self._file.read(struct.calcsize("<" + fmt)))[0]

    def _string(self):
        return self._file.read(self._read("Q")).decode("utf-8")

    def _value(self, typ):
        if typ == 8:
            return self._string()
        if typ == 9:
            subtype, count = self._read("I"), self._read("Q")
            return [self._value(subtype) for _ in range(count)]
        return self._read({0: "B", 1: "b", 2: "H", 3: "h", 4: "I", 5: "i", 6: "f",
                           7: "?", 10: "Q", 11: "q", 12: "d"}[typ])

    def load(self):
        import numpy as np
        import torch
        state = {}
        with self.path.open("rb") as f:
            for tensor in self.tensors:
                f.seek(self.data_offset + tensor["offset"])
                data = f.read(tensor["nbytes"])
                if tensor["ggml_type"] == 30:
                    bits = np.frombuffer(data, dtype="<u2").astype(np.uint32) << 16
                    value = bits.view(np.float32).copy()
                else:
                    # Each Q8_0 block has one IEEE fp16 scale and 32 signed bytes.
                    block = np.frombuffer(data, dtype=np.dtype([("d", "<f2"), ("q", "i1", 32)]))
                    value = (block["q"].astype(np.float32) * block["d"].astype(np.float32)[:, None]).ravel()
                if not np.isfinite(value).all():
                    raise ValueError(f"Nonfinite trained values in {tensor['name']}")
                state[tensor["name"]] = torch.from_numpy(value.reshape(tensor["shape"]))
        return state


def download(name, target):
    """Use exact public snapshot URLs; never select another quantization."""
    target = Path(target)
    target.parent.mkdir(parents=True, exist_ok=True)
    if not target.exists():
        temp = target.with_suffix(target.suffix + ".partial")
        try:
            with urllib.request.urlopen(f"{BASE_URL}/{name}", timeout=120) as source, temp.open("wb") as out:
                while chunk := source.read(1024 * 1024):
                    out.write(chunk)
            temp.replace(target)
        finally:
            temp.unlink(missing_ok=True)
    return target


def sha256(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            h.update(chunk)
    return h.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--download", action="store_true")
    parser.add_argument("--gguf", type=Path, default=ROOT / "weights/model.q8.gguf")
    parser.add_argument("--output", type=Path, default=ROOT / "weights/phonon_q8_source.safetensors")
    args = parser.parse_args()
    if args.download:
        download("model.q8.gguf", args.gguf)
        for name in ("config.json", "tokenizer.json", "tokenizer.model", "default-voice.safetensors"):
            download(name, ROOT / "weights" / name)
    source_sha = sha256(args.gguf)
    if source_sha != Q8_SHA256:
        raise ValueError("The source is not the pinned published q8 checkpoint (SHA-256 mismatch)")
    reader = GGUF(args.gguf)
    state = reader.load()
    from safetensors.torch import save_file, load_file
    args.output.parent.mkdir(parents=True, exist_ok=True)
    save_file(state, args.output, metadata={"source": BASE_URL + "/model.q8.gguf",
              "source_sha256": source_sha, "license": "Apache-2.0",
              "format": "phonon-q8-dequantized-v1", "runtime_revision": RUNTIME_REVISION})
    restored = load_file(args.output)
    import torch
    if set(restored) != set(state) or not all(torch.equal(restored[k], v) for k, v in state.items()):
        raise RuntimeError("Safetensors round-trip changed the trained weights")
    groups = collections.defaultdict(lambda: dict(elements=0, tensors=0, source_bytes=0))
    for item in reader.tensors:
        group = groups[item["name"].split(".")[0]]
        group["elements"] += item["elements"]
        group["tensors"] += 1
        group["source_bytes"] += item["nbytes"]
    report = dict(hf_revision=HF_REVISION, runtime_revision=RUNTIME_REVISION,
                  source_file="model.q8.gguf", source_sha256=source_sha,
                  source_size=args.gguf.stat().st_size, metadata=reader.metadata,
                  data_offset=reader.data_offset, tensor_count=len(state),
                  elements=sum(t.numel() for t in state.values()), groups=dict(groups),
                  safetensors_sha256=sha256(args.output), exact_tensor_roundtrip=True,
                  source_types={"8": "Q8_0", "30": "BF16"}, tensors=reader.tensors)
    (ROOT / "tensor_inventory.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({k: v for k, v in report.items() if k != "tensors"}, indent=2))


if __name__ == "__main__":
    main()
