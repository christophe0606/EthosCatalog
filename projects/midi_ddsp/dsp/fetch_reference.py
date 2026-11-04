#!/usr/bin/env python3
# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Fetch only the Apache DDSP math reference and its license, with provenance."""
import argparse
import hashlib
import json
from pathlib import Path
import re
from urllib.request import urlopen

REVISION = "676aa9d8016e9b0e962d02cb9133081439e4a435"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--revision", default=REVISION)
    parser.add_argument("--output-dir", type=Path, default=Path(__file__).resolve().parent / "reference")
    args = parser.parse_args()
    if not re.fullmatch(r"[0-9a-f]{40}", args.revision):
        parser.error("Use an immutable full Git commit SHA for --revision")
    args.output_dir.mkdir(parents=True, exist_ok=True)
    files = {}
    for remote, name in (("ddsp/core.py", "core.py"), ("LICENSE", "LICENSE")):
        url = f"https://raw.githubusercontent.com/magenta/ddsp/{args.revision}/{remote}"
        with urlopen(url, timeout=60) as response:
            data = response.read()
        (args.output_dir / name).write_bytes(data)
        files[name] = {"url": url, "sha256": hashlib.sha256(data).hexdigest()}
    provenance = {"repository": "https://github.com/magenta/ddsp", "revision": args.revision, "files": files}
    (args.output_dir / "provenance.json").write_text(json.dumps(provenance, indent=2) + "\n", encoding="utf-8")
    print(f"Fetched {len(files)} reference files at {args.revision}")


if __name__ == "__main__":
    main()
