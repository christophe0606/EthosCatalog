#!/usr/bin/env python3
# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Fetch only selected rvTTS/Opus model inputs, verifying their pinned SHA256."""
import hashlib
import json
from pathlib import Path
import urllib.request

HERE = Path(__file__).resolve().parent
REVISION = "b30af35f5366197f1a8845ef55af01a6c7c39a64"
BASE = f"https://raw.githubusercontent.com/ArmstrongSubero/rvTTS/{REVISION}/"


def main():
    manifest = json.loads((HERE / "upstream_manifest.json").read_text())
    destination = HERE / "upstream"
    destination.mkdir(exist_ok=True)
    for name, expected in manifest["files"].items():
        if name == "OPUS-COPYING":
            url = "https://raw.githubusercontent.com/xiph/opus/v1.5.2/COPYING"
        else:
            relative = name if name == "LICENSE" else ("Blobs/"+name if name.endswith(".bin") else "Fargan_x86_Source/"+name)
            url = BASE+relative
        with urllib.request.urlopen(url, timeout=120) as response:
            data = response.read()
        actual = hashlib.sha256(data).hexdigest()
        if actual != expected:
            raise ValueError(f"SHA256 mismatch for {name}: {actual}")
        (destination / name).write_bytes(data)
        print(name, actual, flush=True)


if __name__ == "__main__":
    main()
