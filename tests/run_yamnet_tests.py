# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Build production Stream node tests with a host compiler already on PATH.

For MSVC, run from its Developer Command Prompt; use --compiler clang++ otherwise.
No firmware is built or board connection opened by this host test.
"""
from pathlib import Path
import argparse
import subprocess
import shutil

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--compiler', default='cl' if shutil.which('cl') else 'clang++')
args = parser.parse_args()
output = ROOT / 'out' / 'yamnet-tests'
output.mkdir(parents=True, exist_ok=True)
includes = ['tests/yamnet', 'tests/stream', 'thirdparty/CMSIS-Stream/platform',
            'thirdparty/CMSIS-Stream/platform/posix_runtime', 'projects/yamnet',
            'projects/yamnet/generated', 'projects/yamnet/nodes_impl']
sources = ['tests/test_yamnet_nodes.cpp', 'projects/yamnet/nodes_impl/audio_window.c']
exe = output / 'test_yamnet_nodes.exe'
if Path(args.compiler).stem.lower() == 'cl':
    command = [args.compiler, '/nologo', '/std:c++17', '/EHsc', '/Zc:__cplusplus']
    command += ['/I' + name for name in includes] + sources
    command += ['/Fe:' + str(exe), '/Fo:' + str(output) + '/']
else:
    command = [args.compiler, '-std=c++17', '-O2']
    command += ['-I' + name for name in includes] + sources + ['-o', str(exe)]
subprocess.run(command, cwd=ROOT, check=True)
subprocess.run([str(exe)], cwd=ROOT, check=True)
