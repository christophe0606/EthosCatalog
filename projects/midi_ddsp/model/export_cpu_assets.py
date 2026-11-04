# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Pack the original CPU embeddings/affines/reverb into an OSPI asset."""
import json
import zlib
from pathlib import Path
import numpy as np
from safetensors.numpy import load_file

HERE = Path(__file__).resolve().parent
KEYS = ('e/pitch_emb/embeddings', 'e/instrument_emb/embeddings',
        'e/duration_emb/kernel', 'e/duration_emb/bias',
        's/midi_decoder/instrument_emb/embeddings',
        's/midi_decoder/decoder/q_pitch_emb/kernel',
        's/midi_decoder/decoder/q_pitch_emb/bias',
        's/reverb_module/magnitudes_embedding/embeddings')


def main():
    weights = load_file(HERE/'pretrained.safetensors')
    data = bytearray(); manifest = {}
    header = ['// Copyright 2026 Arm Limited and/or its affiliates.',
              '// SPDX-License-Identifier: Apache-2.0', '#pragma once']
    for key in KEYS:
        values = weights[key]
        if 'instrument_emb/' in key or key.startswith('s/reverb_module/'):
            values = values[:13]  # Exact rows for every supported checkpoint ID.
        data.extend(b'\0'*((-len(data)) % 32))
        offset = len(data)
        data.extend(values.astype('<f4').tobytes())
        manifest[key] = {'offset': offset, 'shape': list(values.shape), 'count': values.size}
        symbol = 'MIDI_ASSET_'+key.upper().replace('/', '_')
        header += [f'#define {symbol}_OFFSET {offset}U', f'#define {symbol}_COUNT {values.size}U']
    report = {'address': 0xC1800000, 'bytes': len(data), 'crc32': zlib.crc32(data),
              'dtype': 'little-endian float32', 'tensors': manifest}
    (HERE/'cpu_assets.bin').write_bytes(data)
    (HERE/'cpu_assets.json').write_text(json.dumps(report, indent=2)+'\n')
    (HERE/'cpu_asset_layout.h').write_text('\n'.join(header)+'\n')
    print('CPU asset bytes', len(data), 'CRC32', hex(report['crc32']))


if __name__ == '__main__':
    main()
