# Copyright (c) 2026 Arm Limited or its affiliates. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
# Generated with AI assistance for this demo.
# Apache-2.0 applies only to new Arm code, not upstream content or model data.
# See LICENSE, LICENSING.md and THIRD_PARTY_NOTICES.md for scope and provenance.

"""Create an E8 RXDS correction of the pack's precompiled OSPI loader.

No programming is performed. The vendor pack is never modified. This small,
hash-guarded patch is needed because the distributed algorithm selects UART7_RX
instead of OSPI1_RXDS on P10_7. Revisit it when updating the Ensemble pack.
"""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE_SHA256 = '0a91d4177081f8c3d21e73e9cc2641ede63e2b760492acbf15493ea7b9a4c3e4'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=Path(
        'C:/ctools_pack/AlifSemiconductor/Ensemble/2.2.1/Flash/algorithms/Ensemble_IS25WX256.FLM'))
    args = parser.parse_args()
    data = args.source.read_bytes()
    if hashlib.sha256(data).hexdigest() != SOURCE_SHA256:
        raise ValueError('Unrecognized vendor algorithm; inspect the new version before patching')
    # Init VMA 0x9a..0xa4, from offline instruction inspection:
    # movs r0,#10; movs r1,#7; movs r4,#1; movs r3,#0x61; mov r2,r4.
    # pinconf_set(port,pin,function,pad) receives arguments in r0..r3.
    original = bytes.fromhex('0a20 0721 0124 6123 2246')
    corrected = bytes.fromhex('0a20 0721 0124 6523 0722')
    if data.count(original) != 1:
        raise ValueError('RXDS Init instruction pattern is not unique')
    offset = data.index(original)
    patched = data[:offset] + corrected + data[offset + len(original):]
    output = ROOT / 'board/flash/DevKit-E8_IS25WX256.FLM'
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(patched)
    evidence = {
        'source': str(args.source), 'source_sha256': SOURCE_SHA256,
        'output': str(output.relative_to(ROOT)),
        'output_sha256': hashlib.sha256(patched).hexdigest(),
        'file_offset': offset, 'init_vma': '0x9a',
        'original_bytes': original.hex(), 'corrected_bytes': corrected.hex(),
        'changes': ['P10_7 function 1 -> 7 (OSPI1_RXDS_C)',
                    'P10_7 pad 0x61 -> 0x65 (match vendor E8 data-pad configuration)'],
        'hardware_validation': 'pending',
    }
    evidence_dir = ROOT / 'validation/flash'
    evidence_dir.mkdir(parents=True, exist_ok=True)
    (evidence_dir / 'e8_flash_algorithm.json').write_text(
        json.dumps(evidence, indent=2) + '\n', encoding='utf-8')
    print('Prepared', output)
    print('Patched Init instruction sequence at file offset', hex(offset))


if __name__ == '__main__':
    main()
