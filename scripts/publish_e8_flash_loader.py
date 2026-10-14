# Copyright (c) 2026 Arm Limited or its affiliates. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
# Generated with AI assistance for this demo.
# Apache-2.0 applies only to new Arm code, not upstream content or model data.
# See LICENSE, LICENSING.md and THIRD_PARTY_NOTICES.md for scope and provenance.

"""Copy the CMSIS-built, source-based E8 algorithm for model-only programming.

Build board/flash/loader/FlashLoader.csolution.yml through CMSIS first. This script never builds or
communicates with the board. The compiled ELF is the FlashOS FLM file format.
"""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    built = ROOT / 'out/FlashLoader/DevKit-E8/FlashLoader/DevKit-E8_IS25WX.axf'
    data = built.read_bytes()
    if data[:6] != b'\x7fELF\x01\x01':
        raise ValueError('Expected a CMSIS-built 32-bit little-endian ELF')
    output = ROOT / 'board/flash/DevKit-E8_IS25WX.FLM'
    output.write_bytes(data)
    sources = ROOT / 'board/flash/loader'
    evidence = {
        'built_image': built.relative_to(ROOT).as_posix(),
        'algorithm': output.relative_to(ROOT).as_posix(),
        'sha256': hashlib.sha256(data).hexdigest(),
        'source_sha256': {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                          for p in sorted(sources.iterdir()) if p.is_file()},
        'source_basis': 'Alif Ensemble 2.2.1 ospi_drv.c, issi_flash.c, IS25WX256.c; Arm CMSIS 5.9.0 FlashOS.h',
        'build_context': 'FlashLoader.FlashLoader+DevKit-E8',
        'hardware_validation': 'pending',
    }
    evidence_dir = ROOT / 'validation/flash'
    evidence_dir.mkdir(parents=True, exist_ok=True)
    (evidence_dir / 'source_flash_loader.json').write_text(
        json.dumps(evidence, indent=2) + '\n', encoding='utf-8')
    print('Published', output)
    print('SHA256', evidence['sha256'])


if __name__ == '__main__':
    main()
