# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Generate and run a host Stream graph using the installed SDS pack API."""
import argparse
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from cmsis_stream.cg.scheduler import CType, Graph, SINT16
from common.nodes import SdsPictureSink, SdsPictureSource, SdsSink, SdsSource
from common.stream_graph import mk_app


def run_test(args, sds_include):
    output = ROOT / "out" / "sds-tests" / "sync"
    sample_type = CType(SINT16)
    replay = SdsSource("replay", sample_type, 2, "replayParams")
    record = SdsSink("record", sample_type, 4, "recordParams")
    picture_replay = SdsPictureSource("picture_replay", "pictureReplayParams", identified=True)
    picture_record = SdsPictureSink("picture_record", "pictureRecordParams", identified=True)
    graph = Graph()
    graph.connect(replay.o, record.i)
    graph.connect(picture_replay["oev0"], picture_record["iev0"])
    schedule = mk_app(graph, output, {}, {})
    assert schedule.scheduleLength == 3

    includes = [sds_include, output, *[ROOT / path for path in (
        "tests/sds", "tests/stream", "tests/rtos", "common/stream", "common/nodes_impl/c",
        "thirdparty/CMSIS-Stream/platform", "thirdparty/CMSIS-Stream/platform/posix_runtime",
    )]]
    binary = output / ("test_sds_nodes.exe" if os.name == "nt" else "test_sds_nodes")
    sources = [ROOT / "tests/test_sds_nodes.cpp", output / "scheduler_app.cpp"]
    if args.vcvars:
        batch = output / "compile.cmd"
        batch.write_text(
            f'@echo off\ncall "{args.vcvars.resolve()}" x64 >nul\n'
            'if errorlevel 1 exit /b %errorlevel%\n'
            'cl /nologo /std:c++17 /EHsc /O2 '
            + ' '.join(f'/I"{path}"' for path in includes) + ' '
            + ' '.join(f'"{path}"' for path in sources) + f' /Fe:"{binary}"\n'
        )
        subprocess.run(["cmd", "/d", "/c", str(batch)], cwd=output, check=True)
    else:
        thread_flags = [] if os.name == "nt" else ["-pthread"]
        subprocess.run([args.cxx, "-std=c++17", "-O2", *thread_flags,
                        *[f"-I{path}" for path in includes],
                        *map(str, sources), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=15)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--pack-root", type=Path, default=os.environ.get("CMSIS_PACK_ROOT"))
    parser.add_argument("--sds-version", default="3.1.0")
    parser.add_argument("--cxx", default="clang++")
    parser.add_argument("--vcvars", type=Path)
    args = parser.parse_args()
    if args.pack_root is None:
        parser.error("Specify --pack-root or set CMSIS_PACK_ROOT")
    sds_include = args.pack_root.resolve() / "ARM" / "SDS" / args.sds_version / "sds" / "include"
    if not (sds_include / "sds.h").is_file():
        parser.error(f"SDS pack header missing in {sds_include}")
    run_test(args, sds_include)


if __name__ == "__main__":
    main()
