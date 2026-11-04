# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Create deterministic corrected method fixtures, including OSPI harmonic data."""
import json
import zlib
from pathlib import Path
import numpy as np
import torch
import yaml
import torchao.quantization.pt2e.quantize_pt2e
import executorch.exir.passes.dim_order_ops_registry
from model import load_model
from deployment import methods, render_controls, score, FRAMES

HERE = Path(__file__).resolve().parent


def main():
    torch.set_num_threads(1)
    model = load_model(); ops = methods(model)
    capture = {name: [] for name in ops}
    with torch.no_grad():
        render_controls(model, ops, *score(0, FRAMES, 9100), capture=capture)
    blob = bytearray(); arrays = {}; manifest = {}
    header = ['// Copyright 2026 Arm Limited and/or its affiliates.',
              '// SPDX-License-Identifier: Apache-2.0', '#pragma once']
    with torch.no_grad():
        for name, op in ops.items():
            program = torch.export.load(HERE/f'{name}_quantized.pt2')
            graph = program.module(check_guards=False)
            x = capture[name][len(capture[name])//2]
            batch = int(program.example_inputs[0][0].shape[0]) if name=='precondition' else 1
            if batch>1:
                indices=np.linspace(0,len(capture[name])-1,batch).astype(int)
                x=torch.cat([capture[name][i] for i in indices])
                from batch_precondition import RowLinear
                y=RowLinear(graph).run(x)
                if isinstance(y,tuple): y=y[0]
            else:
                y=graph(x)
            expected = op(x)
            info = {'float_max_absolute_error': float((y-expected).abs().max())}
            if batch>1:
                info['batch']=batch
                info['golden_evaluation']='Heterogeneous score frames; rowwise linear summation of saved quantized graph, preserving single-frame host arithmetic. Native GEMM differences recorded separately in precondition_batch_validation.json.'
            for suffix, value in [('input', x), ('expected', y)]:
                data = value.numpy().astype('<f4'); key = name+'_'+suffix
                arrays[key] = data; blob.extend(b'\0'*((-len(blob)) % 32))
                info[suffix] = {'offset': len(blob), 'count': data.size, 'shape': list(data.shape)}
                blob.extend(data.tobytes())
                header += [f'#define MIDI_FIXTURE_{key.upper()}_OFFSET {info[suffix]["offset"]}U',
                           f'#define MIDI_FIXTURE_{key.upper()}_COUNT {data.size}U']
                if name != 'harmonic':
                    header.append(f'static const float midi_{key}[{data.size}] = {{'+','.join(f'{v:.9e}f' for v in data.flatten())+'};')
            manifest[name] = info
    report = {'preprocessing': 'Corrected upstream clip/rest conventions',
              'score': 'instrument0;score(seed9100);1000frames;4notes includingrest;middle held-float input for each method',
              'address': 0xC1C00000, 'bytes': len(blob), 'crc32': zlib.crc32(blob),
              'methods': manifest}
    (HERE/'benchmark_assets.bin').write_bytes(blob)
    (HERE/'benchmark_fixtures.json').write_text(json.dumps(report, indent=2)+'\n')
    (HERE/'benchmark_fixtures.h').write_text('\n'.join(header)+'\n')
    np.savez_compressed(HERE/'benchmark_fixtures.npz', **arrays)
    path = HERE/'network.yml'; config = yaml.safe_load(path.read_text())
    config['network']['assets']['benchmark_fixtures'] = {'file': 'model/benchmark_assets.bin', 'address': report['address']}
    path.write_text(yaml.safe_dump(config, sort_keys=False))
    print('Corrected benchmark fixture asset bytes', len(blob), 'CRC32', hex(report['crc32']))


if __name__ == '__main__':
    main()
