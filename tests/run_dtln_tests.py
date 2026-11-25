# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Build the DTLN SDS graph with host doubles for FFT, inference and SDS I/O."""
import argparse
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--vcvars", type=Path)
    parser.add_argument("--cxx", default="clang++")
    parser.add_argument("--pack-root", type=Path, default=os.environ.get("CMSIS_PACK_ROOT"))
    parser.add_argument("replays", nargs="*", type=Path, help="Optional Dtln_In.N.sds files")
    args = parser.parse_args()
    if args.pack_root is None:
        parser.error("Specify --pack-root or set CMSIS_PACK_ROOT")
    sds_include = args.pack_root.resolve() / "ARM/SDS/3.1.0/sds/include"
    if not (sds_include / "sds.h").exists():
        parser.error(f"SDS pack header missing in {sds_include}")
    output = ROOT / "out/dtln-tests"
    output.mkdir(exist_ok=True, parents=True)
    binary = output / "dtln_stream.exe"
    includes = [sds_include, *[ROOT / path for path in (
        "tests/dtln", "tests/rtos", "projects/dtln_aec", "projects/dtln_aec/generated",
        "projects/dtln_aec/nodes_impl", "common/stream", "common/nodes_impl/c",
        "thirdparty/CMSIS-Stream/platform", "thirdparty/CMSIS-Stream/platform/posix_runtime")]]
    sources = [ROOT / path for path in (
        "tests/test_dtln_stream.cpp", "projects/dtln_aec/generated/scheduler_app.cpp",
        "projects/dtln_aec/generated/app_params.c", "projects/dtln_aec/stream_init.cpp",
        "projects/dtln_aec/nodes_impl/waveform_display.cpp",
        "thirdparty/CMSIS-Stream/platform/posix_runtime/stream_event_queue.cpp",
        "thirdparty/CMSIS-Stream/platform/posix_runtime/stream_runtime_init.cpp")]
    if args.vcvars:
        batch = output / "compile.cmd"
        architecture = "-arch=x64" if args.vcvars.name.lower() == "vsdevcmd.bat" else "x64"
        batch.write_text(f'@echo off\ncall "{args.vcvars.resolve()}" {architecture} >nul\n'
            'if errorlevel 1 exit /b %errorlevel%\n'
            + 'cl /nologo /std:c++20 /EHsc /O2 /fp:fast /TP /D__PRETTY_FUNCTION__=__FUNCSIG__ '
            + ' '.join('/I"' + str(path) + '"' for path in includes) + ' '
            + ' '.join('"' + str(path) + '"' for path in sources)
            + ' /Fe:"' + str(binary) + '"\n')
        subprocess.run(["cmd", "/d", "/c", str(batch)], cwd=output, check=True)
    else:
        thread_flags = [] if os.name == "nt" else ["-pthread"]
        subprocess.run([args.cxx, "-std=c++17", "-O2", "-ffast-math", *thread_flags,
                        *[f"-I{path}" for path in includes], *map(str, sources),
                        "-o", str(binary)], check=True)
    subprocess.run([str(binary), *map(str, args.replays)], check=True, timeout=30)


if __name__ == "__main__":
    main()
