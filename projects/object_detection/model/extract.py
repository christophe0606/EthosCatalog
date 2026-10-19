#!/usr/bin/env python3
# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Extract only the original pre-Vela model and RGB sample from an examples checkout."""
import argparse
import hashlib
from pathlib import Path
import re


def array(path, name):
    text = path.read_text(encoding="utf-8")
    match = re.search(r"\b" + name + r"\[\][^=]*=\s*\{(.*?)\};", text, re.S)
    if not match:
        raise ValueError(f"Array {name} not found in {path.name}")
    return bytes(int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]{1,2})\b", match[1]))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, required=True)
    args = parser.parse_args()
    dest = Path(__file__).resolve().parent
    model = array(args.source_dir / "src/yolo-fastest_192_face_v4.tflite.cpp", "nn_model")
    if len(model) != 510280 or model[4:8] != b"TFL3":
        raise ValueError("Unexpected original model")
    image = array(args.source_dir / "src/sample_image.cpp", "im0")
    if len(image) != 192 * 192 * 3:
        raise ValueError("Unexpected RGB sample size")
    for name, data in (("yolo-fastest_192_face_v4.tflite", model), ("sample_image.rgb", image)):
        (dest / name).write_bytes(data)
        print(name, len(data), hashlib.sha256(data).hexdigest())


if __name__ == "__main__":
    main()
