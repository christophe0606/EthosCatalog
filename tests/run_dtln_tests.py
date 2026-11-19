# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Build production DTLN Stream nodes with a host FFT and inference test double."""
import argparse
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--vcvars", type=Path)
    parser.add_argument("--cxx", default="clang++")
    args = parser.parse_args()
    output = ROOT / "out/dtln-tests"
    output.mkdir(exist_ok=True, parents=True)
    binary = output / "dtln_stream.exe"
    includes = [ROOT / path for path in (
        "tests/dtln", "projects/dtln_aec", "projects/dtln_aec/generated",
        "projects/dtln_aec/nodes_impl", "common/stream", "thirdparty/CMSIS-Stream/platform",
        "thirdparty/CMSIS-Stream/platform/posix_runtime")]
    sources = [ROOT / path for path in (
        "tests/test_dtln_stream.cpp", "projects/dtln_aec/generated/scheduler_app.cpp",
        "projects/dtln_aec/generated/app_params.c")]
    if args.vcvars:
        batch = output / "compile.cmd"
        batch.write_text('@echo off\ncall "' + str(args.vcvars.resolve()) + '" -arch=x64 >nul\n'
            + 'cl /nologo /std:c++20 /EHsc /O2 /fp:fast /TP '
            + ' '.join('/I"' + str(path) + '"' for path in includes) + ' '
            + ' '.join('"' + str(path) + '"' for path in sources)
            + ' /Fe:"' + str(binary) + '"\n')
        subprocess.run(["cmd", "/d", "/c", str(batch)], cwd=output, check=True)
    else:
        subprocess.run([args.cxx, "-std=c++17", "-O2", "-ffast-math",
                        *[f"-I{path}" for path in includes], *map(str, sources),
                        "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
