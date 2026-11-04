# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Compile and run the production MIDI-to-control-window frontend."""
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cxx', default='clang++')
    parser.add_argument('--vcvars', type=Path)
    args = parser.parse_args()
    output = ROOT/'out/midi-ddsp-tests'
    output.mkdir(parents=True, exist_ok=True)
    run = Path(tempfile.mkdtemp(prefix='score-', dir=output))
    include = ROOT/'projects/midi_ddsp'
    sources = [ROOT/'tests/test_midi_ddsp_score_windows.cpp', include/'player/score_windows.cpp', include/'player/midi_file.cpp']
    binary = run/'score_tests.exe'
    if args.vcvars:
        batch = run/'compile.cmd'
        batch.write_text('@echo off\ncall "'+str(args.vcvars.resolve())+'" >nul\n'
                         'cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /I"'+str(include)+'" '
                         +' '.join('"'+str(p)+'"' for p in sources)+' /Fe:"'+str(binary)+'"\n')
        subprocess.run(['cmd', '/d', '/c', str(batch)], cwd=run, check=True)
    else:
        subprocess.run([args.cxx, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', f'-I{include}', *map(str,sources), '-o', str(binary)], cwd=run, check=True)
    subprocess.run([str(binary), str(include/'assets/single_voice.mid'), str(include/'assets/all_instruments.mid')], check=True)


if __name__ == '__main__':
    main()
