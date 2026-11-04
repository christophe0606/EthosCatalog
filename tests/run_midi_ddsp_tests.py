# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Build and run the production MIDI reader on a host C++ compiler."""
import argparse
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default="clang++")
    parser.add_argument("--vcvars", type=Path)
    args = parser.parse_args()
    output = ROOT / "out/midi-ddsp-tests"
    output.mkdir(parents=True, exist_ok=True)
    include = ROOT / "projects/midi_ddsp"
    for name, backend in (("midi", "midi_file"), ("voices", "voice_allocator")):
        binary = output / f"test_midi_ddsp_{name}.exe"
        sources = [ROOT / f"tests/test_midi_ddsp_{name}.cpp", include / f"player/{backend}.cpp"]
        if args.vcvars:
            batch = output / "compile.cmd"
            batch.write_text('@echo off\ncall "' + str(args.vcvars.resolve()) + '" >nul\n'
                             + 'cl /nologo /std:c++17 /EHsc /W4 /O2 /I"' + str(include) + '" '
                             + ' '.join('"' + str(path) + '"' for path in sources)
                             + ' /Fe:"' + str(binary) + '"\n', encoding="utf-8")
            subprocess.run(["cmd", "/d", "/c", str(batch)], cwd=output, check=True)
        else:
            subprocess.run([args.cxx, "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
                            f"-I{include}", *map(str, sources), "-o", str(binary)], check=True)
        fixtures = [include / f"assets/{fixture}.mid" for fixture in ("single_voice", "all_instruments")] if name == "midi" else []
        subprocess.run([str(binary), *map(str, fixtures)], check=True)


if __name__ == "__main__":
    main()
