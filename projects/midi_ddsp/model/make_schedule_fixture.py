# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Make a deterministic LCG-quantile complete-schedule benchmark trace.

Reads accepted PTQ caches, never modifies deployment assets or configuration.
"""
import argparse
import hashlib
import json
from pathlib import Path
import zlib
import numpy as np
import torch
import torchao.quantization.pt2e.quantize_pt2e
import executorch.exir.passes.dim_order_ops_registry
from model import load_model
from deployment import methods, render_controls, score, FRAMES, load_saved_method

HERE = Path(__file__).resolve().parent
SELECTED = (0, 249, 250, 499, 500, 749, 750, 999)


class Trace:
    def __init__(self, name, op, records):
        self.name, self.op, self.records, self.calls = name, op, records, 0

    def __call__(self, x):
        output = self.op(x)
        selected = range(4) if self.name.startswith('expression_') else SELECTED
        if self.name != 'harmonic' and self.calls in selected:
            self.records.setdefault(self.name, []).append((self.calls, output.numpy().copy()))
        self.calls += 1
        return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', type=Path, default=HERE.parents[2]/'.flow/midi_ddsp/schedule_fixture')
    args = parser.parse_args()
    torch.set_num_threads(1)
    model = load_model()
    names = list(methods(model))
    records = {}
    ops = {name: Trace(name, load_saved_method(name,HERE/f'{name}_quantized.pt2'), records)
           for name in names}
    inputs = list(score(0, FRAMES, 9100))
    state = 9100
    uniform = []
    for _ in range(FRAMES):
        state = (1664525*state + 1013904223) & 0xffffffff
        value = np.float32(state >> 8) * np.float32(2**-24)
        uniform.append(np.float32(np.float32(value * np.float32(.98)) + np.float32(.01)))
    inputs[-1] = torch.from_numpy(np.asarray(uniform, np.float32).reshape(1, FRAMES, 1))
    with torch.no_grad():
        # Keep the independent one-frame numerical goldens when the saved ABI
        # batches rows. This intentionally checks batching against prior math.
        raw, pitch, expression, _ = render_controls(model, ops, *inputs,precondition_batch=1)
    arrays = {'uniform_first_eight': np.asarray(uniform[:8], np.float32),
              'expected_pitch': pitch.numpy().reshape(-1),
              'expected_expression': expression.numpy().reshape(-1),
              'expected_raw_selected': raw[0, list(SELECTED)].numpy().reshape(-1)}
    selection = {}
    for name, captured in records.items():
        arrays['expected_'+name] = np.concatenate([item[1].reshape(-1) for item in captured])
        selection[name] = {'calls': [item[0] for item in captured], 'output_width': captured[0][1].size}
    header = ['// Copyright 2026 Arm Limited and/or its affiliates.',
              '// SPDX-License-Identifier: Apache-2.0', '#pragma once',
              'static constexpr unsigned midi_schedule_frames = 1000;',
              'static constexpr unsigned midi_schedule_seed = 9100;',
              'static const unsigned midi_schedule_selected_frames[] = {'+','.join(map(str, SELECTED))+'};']
    for name, values in arrays.items():
        header.append('static const float midi_schedule_'+name+'[] = {'+
                      ','.join(f'{float(value):.9e}f' for value in values)+'};')
    by_region = []
    for region, note in enumerate(inputs[0][0]):
        values = pitch[0, inputs[3][0] == region, 0]
        by_region.append({'region': region, 'midi_note': int(note),
                          'pitch_mean': float(values.mean()), 'pitch_std': float(values.std(unbiased=False))})
    files = [HERE/name for name in ('pretrained.safetensors', 'model.py', 'deployment.py', 'make_schedule_fixture.py')]
    files += [HERE/f'{name}_quantized.{ext}' for name in names for ext in ('pt2', 'safetensors')]
    report = {'instrument_id': 0, 'frames': FRAMES, 'notes': [48, 55, 0, 60], 'note_frame_lengths': [250]*4,
              'uniform': 'LCGseed9100: state=(1664525*state+1013904223)mod2^32; float32(float32(float32(state>>8)*2^-24)*.98f)+.01f; separate float32 mul/add rounding',
              'method_calls': {name: op.calls for name, op in ops.items()}, 'selected_outputs': selection,
              'raw': {'crc32_little_endian_f32': zlib.crc32(raw.numpy().astype('<f4').tobytes()),
                      'min': float(raw.min()), 'max': float(raw.max()), 'mean': float(raw.mean()),
                      'rms': float(raw.square().mean().sqrt()),
                      'component_mean': raw[0].mean(0).tolist(), 'component_rms': raw[0].square().mean(0).sqrt().tolist()},
              'pitch_regions': by_region,
              'file_sha256': {path.name: hashlib.sha256(path.read_bytes()).hexdigest() for path in files},
              'limits': 'Independent saved-cache Python schedule trace; CPU/NPU arithmetic may differ. CRC is provenance only, not a permitted approximate accuracy test. No audio or real-time claim.'}
    args.output_dir.mkdir(parents=True, exist_ok=True)
    np.savez_compressed(args.output_dir/'schedule_controls.npz', raw=raw.numpy(), pitch=pitch.numpy(), **arrays)
    (args.output_dir/'schedule_fixture.h').write_text('\n'.join(header)+'\n', encoding='utf-8')
    (args.output_dir/'schedule_fixture.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print('Schedule fixture complete', args.output_dir, 'method calls', report['method_calls'], flush=True)


if __name__ == '__main__':
    main()
