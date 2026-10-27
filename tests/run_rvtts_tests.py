# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Build the real RVTTS event/playback nodes against the CMSIS Stream host API."""
import argparse
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cxx', default='clang++')
    args = parser.parse_args()
    build = ROOT / 'out/rvtts-tests'
    build.mkdir(parents=True, exist_ok=True)
    for project, fargan in (('rvtts_lpc', 0), ('rvtts_fargan', 1)):
        includes = [ROOT / p for p in ('tests/rvtts', f'projects/{project}',
                    f'projects/{project}/generated', f'projects/{project}/nodes_impl',
                    'thirdparty/CMSIS-Stream/platform',
                    'thirdparty/CMSIS-Stream/platform/posix_runtime')]
        binary = build / (project + '.exe')
        subprocess.run([args.cxx, '-std=c++17', '-O2', f'-DRVTTS_TEST_FARGAN={fargan}',
                        *[f'-I{p}' for p in includes], str(ROOT / 'tests/test_rvtts_streaming.cpp'),
                        '-o', str(binary)], check=True, cwd=build)
        result = subprocess.run([str(binary)], check=True, capture_output=True, text=True)
        (build / (project + '.log')).write_text(result.stdout)
        print(project + ': ' + result.stdout.splitlines()[-1])

if __name__ == '__main__':
    main()
