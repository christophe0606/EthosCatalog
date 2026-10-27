#!/usr/bin/env python3
# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Fetch only the selected licensed rvTTS inputs, pinned by SHA256."""
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
        relative = name if name in ("LICENSE", "README.md") else ("Blobs/"+name if name.endswith(".bin") else "LPC_x86_Source/"+name)
        with urllib.request.urlopen(BASE+relative, timeout=60) as response:
            data = response.read()
        actual = hashlib.sha256(data).hexdigest()
        if actual != expected:
            raise ValueError(f"SHA256 mismatch for {relative}: {actual}")
        (destination / name).write_bytes(data)
        print(name, actual, flush=True)


if __name__ == "__main__":
    main()
