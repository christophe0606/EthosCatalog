#!/usr/bin/env python3
# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Compare only FARGAN and COPYING from the 7.8 MB official Opus release.

Only those two selected files are extracted. No external project is installed.
"""
import hashlib
import io
import json
from pathlib import Path
import re
import tarfile
import urllib.request
import numpy as np

HERE = Path(__file__).resolve().parent
URL = "https://downloads.xiph.org/releases/opus/opus-1.5.2.tar.gz"


class Bounded(io.RawIOBase):
    def __init__(self, source):
        self.source, self.count = source, 0

    def read(self, n=-1):
        if self.count > 9_000_000:
            raise RuntimeError("Stopped at the 9 MB network limit")
        data = self.source.read(n)
        self.count += len(data)
        return data


def main():
    report = {"url": URL, "selected_members": {}}
    arrays = lambda text: {name: np.fromstring(re.sub(r'(?<=[0-9])f\b', '', values), sep=',')
                           for name, values in re.findall(r'const\s+\w+\s+(\w+)\[\d+\]\s*=\s*\{(.*?)\};', text, re.S)}
    imported = (HERE / "upstream/fargan_data.c").read_bytes()
    with urllib.request.urlopen(URL, timeout=60) as response:
        source = Bounded(response)
        with tarfile.open(fileobj=source, mode="r|gz") as archive:
            for member in archive:
                if member.name.endswith(("/dnn/fargan_data.c", "/COPYING")):
                    original = archive.extractfile(member).read()
                    report["selected_members"][member.name] = {"bytes": len(original), "sha256": hashlib.sha256(original).hexdigest()}
                    if member.name.endswith("/COPYING"):
                        (HERE / "upstream/OPUS-COPYING").write_bytes(original)
                    else:
                        official, rvtts = arrays(original.decode()), arrays(imported.decode())
                        report["arrays"] = {name: {"values": len(a), "exact_match": name in rvtts and np.array_equal(a, rvtts[name])}
                                            for name, a in official.items()}
                        report["rvtts_sha256"] = hashlib.sha256(imported).hexdigest()
        report["compressed_bytes_read"] = source.count
    (HERE / "official_provenance.json").write_text(json.dumps(report, indent=2)+"\n")
    print(report)


if __name__ == "__main__":
    main()
