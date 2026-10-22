#!/usr/bin/env python3
# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Record sizes from the generated Ethos-U binary stream (host, no firmware)."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[3]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pte", type=Path, default=Path(__file__).resolve().parents[1] / "ai_layer/model.pte")
    args = parser.parse_args()
    data = args.pte.read_bytes()
    start = data.index(b"vela_bin_stream\x00")
    position = start
    blocks = {}
    while True:
        name = data[position:position+16].rstrip(b"\x00").decode("ascii")
        length, r0, r1, r2 = struct.unpack_from("<IIII", data, position+16)
        assert not (r0 or r1 or r2)
        payload = data[position+32:position+32+length]
        blocks[name] = {"bytes": length}
        if name == "scratch_size":
            blocks[name]["arena_bytes"] = struct.unpack("<I", payload)[0]
        if name in ("inputs", "outputs"):
            count = struct.unpack_from("<I", payload)[0]
            entries = [struct.unpack_from("<9i", payload, 4+i*36) for i in range(count)]
            blocks[name]["tensors"] = [{"shape_6d": list(v[:6]), "element_bytes": v[6],
                                         "offset_bytes": v[7], "region": v[8]} for v in entries]
        position += 32 + (length+15)//16*16
        if name == "vela_end_stream":
            break
    report = {"pte_sha256": hashlib.sha256(data).hexdigest(), "pte_bytes": len(data), "delegate_blocks": blocks,
              "scope": "PTE binary stream sizes; not measured runtime allocator peaks or linked firmware memory"}
    Path(__file__).with_name("pte_sizes.json").write_text(json.dumps(report, indent=2)+"\n", encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
