#!/usr/bin/env python3
# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Prepare bounded CMSIS flash operations; never access the target.

The model-only generator first checks the current PTE, caches, configuration
and assets. Each emitted run file then programs one chunk through CMSIS flash.
This avoids a single long programming operation exceeding tool timeouts.
"""
from __future__ import annotations

import argparse
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import subprocess
import sys

import yaml

PROJECT = Path(__file__).resolve().parents[1]
ROOT = PROJECT.parents[1]
ERASE_BYTES = 128 * 1024
DEFAULT_CHUNK_BYTES = 8 * 1024 * 1024


def digest(data):
    return hashlib.sha256(data).hexdigest()


def prepare_parts(run, output_dir, chunk_bytes):
    """Split already validated model-only outputs without sharing erase sectors."""
    if chunk_bytes <= 0 or chunk_bytes % ERASE_BYTES:
        raise ValueError("Chunk size must be a positive multiple of 128 KiB")
    images = []
    sectors = []
    for image in run["output"]:
        if image.get("type") != "bin" or image.get("load") != "image":
            raise ValueError("Only model-only binary image outputs may be split")
        path = Path(image["file"]).resolve()
        if not path.is_relative_to(ROOT):
            raise ValueError("Model images must remain inside this repository")
        data = path.read_bytes()
        start = image["load-offset"]
        if not data or start % ERASE_BYTES:
            raise ValueError("Each model image must be nonempty and start at a 128 KiB boundary")
        stop = start + ((len(data) + ERASE_BYTES - 1) // ERASE_BYTES) * ERASE_BYTES
        for other_start, other_stop in sectors:
            if start < other_stop and other_start < stop:
                raise ValueError("Independent flash images must not share an erase sector")
        sectors.append((start, stop))
        images.append((image, path, data))

    # Validate every source before writing any new part files.
    output_dir.mkdir(parents=True, exist_ok=True)
    manifest = {"erase_bytes": ERASE_BYTES, "chunk_bytes": chunk_bytes, "images": [], "operations": []}
    for index, (image, path, data) in enumerate(images):
        source_sha = digest(data)
        manifest["images"].append({"file": path.relative_to(ROOT).as_posix(),
                                   "address": image["load-offset"], "bytes": len(data),
                                   "sha256": source_sha})
        for part_index, offset in enumerate(range(0, len(data), chunk_bytes)):
            part = data[offset:offset + chunk_bytes]
            stem = f"{index:02d}-{path.stem}-{source_sha[:12]}-{part_index:02d}"
            binary = output_dir / (stem + ".bin")
            config = output_dir / (stem + ".cbuild-run.yml")
            binary.write_bytes(part)
            part_run = deepcopy(run)
            part_run["generated-by"] = "phonon_31m_en/model/prepare_flash_chunks.py (model-only part)"
            part_run["output"] = [{**image, "file": binary.as_posix(),
                                   "load-offset": image["load-offset"] + offset,
                                   "info": f"{path.name} bytes {offset}:{offset + len(part)}, SHA256 {digest(part)}"}]
            config.write_text(yaml.safe_dump({"cbuild-run": part_run}, sort_keys=False), encoding="utf-8")
            manifest["operations"].append({"run": config.relative_to(ROOT).as_posix(),
                                            "file": binary.relative_to(ROOT).as_posix(),
                                            "source": path.relative_to(ROOT).as_posix(),
                                            "source_sha256": source_sha, "offset": offset,
                                            "address": image["load-offset"] + offset,
                                            "bytes": len(part), "sha256": digest(part)})
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", type=Path, required=True, help="Application cbuild-run with the 64 MiB algorithm region")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "out/phonon-flash-parts")
    parser.add_argument("--chunk-bytes", type=int, default=DEFAULT_CHUNK_BYTES)
    args = parser.parse_args()
    output_dir = args.output_dir.resolve()
    if not output_dir.is_relative_to((ROOT / "out").resolve()):
        raise ValueError("Programming parts must be generated inside the repository out/ directory")
    if args.chunk_bytes <= 0 or args.chunk_bytes % ERASE_BYTES:
        parser.error("--chunk-bytes must be a positive multiple of 131072")
    output_dir.mkdir(parents=True, exist_ok=True)
    full_config = output_dir / "model-full.cbuild-run.yml"
    algorithm = ROOT / "board/flash/DevKit-E8_IS25WX.FLM"
    subprocess.run([sys.executable, str(ROOT / "scripts/create_model_flash.py"),
                    "--solution", str(ROOT / "EthosCatalog.csolution.yml"),
                    "--project", str(PROJECT / "phonon_31m_en.cproject.yml"),
                    "--run", str(args.run.resolve()), "--algorithm", str(algorithm),
                    "--output", str(full_config)], cwd=ROOT, check=True)
    run = yaml.safe_load(full_config.read_text(encoding="utf-8"))["cbuild-run"]
    manifest = prepare_parts(run, output_dir, args.chunk_bytes)
    manifest["algorithm"] = {"file": algorithm.relative_to(ROOT).as_posix(),
                             "sha256": digest(algorithm.read_bytes())}
    generation = ROOT / "ai_layer/generation.json"
    manifest["generation"] = json.loads(generation.read_text(encoding="utf-8"))
    manifest_file = output_dir / "manifest.json"
    manifest_file.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Prepared {len(manifest['operations'])} independent CMSIS flash operations: {manifest_file}")
    print("Program run files in manifest order using CMSIS flash after ProgramModel handoff.")
    print("Then reload Release and verify complete PTE/voice/BOS CRCs; no part alone proves completion.")


if __name__ == "__main__":
    main()
