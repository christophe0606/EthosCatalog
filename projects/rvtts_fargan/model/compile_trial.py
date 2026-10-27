#!/usr/bin/env python3
# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Run a bounded full-project ExecuTorch/Vela compilation and record its result."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time


HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--timeout-seconds", type=int, default=600)
    args = parser.parse_args()
    log = ROOT / "tmp/rvtts-fargan-compile.log"
    log.parent.mkdir(exist_ok=True)
    command = [sys.executable, str(ROOT / "scripts/create_ai_layer.py"), "--project", "rvtts_fargan"]
    sources = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
               for p in sorted(HERE.glob("*_quantized.pt2"))}
    start = time.monotonic()
    status = "failed"
    with log.open("w") as stream:
        process = subprocess.Popen(command, cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT)
        try:
            result = process.wait(timeout=args.timeout_seconds)
            status = "passed" if result == 0 else "failed"
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
            result = 124
            status = "timeout"
    report = {"status": status, "elapsed_seconds": time.monotonic()-start,
              "timeout_seconds": args.timeout_seconds, "returncode": result,
              "command": "uv run scripts/create_ai_layer.py --project rvtts_fargan",
              "log": log.relative_to(ROOT).as_posix(), "input_graph_sha256": sources}
    (HERE / "compiler_trial.json").write_text(json.dumps(report, indent=2)+"\n")
    print(json.dumps(report, indent=2), flush=True)
    return result


if __name__ == "__main__":
    raise SystemExit(main())
