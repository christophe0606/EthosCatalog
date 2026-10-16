#!/usr/bin/env python3
# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Validate and reuse the imported int8 cache; never calibrate or requantize."""
import argparse
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts"))
from project_config import DEFAULT_SOLUTION, selected_project
from quantized_model import load_quantized_program, sha256


def reuse(target, project_dir, network):
    from safetensors import safe_open
    from model import SOURCE, SOURCE_SHA256
    if sha256(SOURCE) != SOURCE_SHA256:
        raise ValueError("The original TFLite source has changed")
    path = project_dir / network["safetensors"]
    if not path.is_file() or not (project_dir / network["graph"]).is_file():
        raise ValueError("Missing converted cache; run model/convert.py with a TensorFlow reference Python")
    with safe_open(path, framework="pt", device="cpu") as cache:
        metadata = cache.metadata()
    for key, file in (("source_sha256", network["model"]), ("quantizer_sha256", network["quantize"])):
        if metadata.get(key) != sha256(project_dir / file):
            raise ValueError("Conversion source changed; regenerate with model/convert.py")
    load_quantized_program(target, project_dir, network)
    print("[kws_micronet_l] already quantized: cache verified; no calibration or quantization", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--solution", type=Path, default=DEFAULT_SOLUTION)
    parser.add_argument("--project", default="kws_micronet_l")
    args = parser.parse_args()
    _, solution, project, _, network = selected_project(args.solution, args.project)
    if project.parent != Path(__file__).resolve().parents[1]:
        parser.error("This script belongs to kws_micronet_l")
    npu = solution["mlops"]["npu"]
    reuse(f"{npu['type'].lower()}-{npu['macs']}", project.parent, network)


if __name__ == "__main__":
    main()
