# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Check the production muted-rest shortcut against normal Synth intervals."""
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
    run = Path(tempfile.mkdtemp(prefix='muted-', dir=output))
    backend = ROOT/'projects/midi_ddsp/nodes_impl/c'
    sources = [ROOT/'tests/test_midi_ddsp_muted_state.cpp', backend/'ddsp_synth.cpp']
    binary = run/'muted_tests.exe'
    if args.vcvars:
        batch = run/'compile.cmd'
        batch.write_text('@echo off\ncall "'+str(args.vcvars.resolve())+'" >nul\n'
                         'cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /I"'+str(backend)+'" '
                         +' '.join('"'+str(p)+'"' for p in sources)+' /Fe:"'+str(binary)+'"\n')
        subprocess.run(['cmd', '/d', '/c', str(batch)], cwd=run, check=True)
    else:
        subprocess.run([args.cxx, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-I'+str(backend), *map(str,sources), '-o', str(binary)], cwd=run, check=True)
    subprocess.run([str(binary)], check=True)

if __name__ == '__main__':
    main()
