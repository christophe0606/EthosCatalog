# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Measure float model pitch variation across independent categorical draws.

These few synthetic seeds give context for closed-loop PTQ trajectory changes;
they are not confidence bounds, perceptual acceptance or a held-out dataset.
"""
import argparse
import hashlib
import json
from pathlib import Path
import torch
from model import load_model
from deployment import methods, render_controls, score, FRAMES

HERE = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--checkpoint-dir', type=Path, default=HERE.parents[2]/'.flow/midi_ddsp/pitch_variability')
    parser.add_argument('--seed-offsets', type=int, nargs='+', default=[100, 200])
    parser.add_argument('--output', type=Path, default=HERE/'pitch_variability.json')
    args = parser.parse_args()
    torch.set_num_threads(1)
    model = load_model()
    ops = methods(model)
    hashes = {name: hashlib.sha256((HERE/name).read_bytes()).hexdigest()
              for name in ('pretrained.safetensors', 'model.py', 'deployment.py', 'validate_pitch_variability.py')}
    args.checkpoint_dir.mkdir(parents=True, exist_ok=True)
    results = []
    with torch.no_grad():
        for instrument in range(13):
            for offset in args.seed_offsets:
                seed = 9100 + instrument + offset
                path = args.checkpoint_dir/f'instrument_{instrument:02}_seed_{seed}.json'
                if path.exists():
                    result = json.loads(path.read_text())
                    if result['source_sha256'] != hashes:
                        raise ValueError(f'Stale checkpoint: {path}; choose another directory')
                    results.append(result)
                    continue
                inputs = score(instrument, FRAMES, seed)
                _, pitch, _, _ = render_controls(model, ops, *inputs)
                regions = []
                for region, note in enumerate(inputs[0][0]):
                    values = pitch[0, inputs[3][0] == region, 0] * 100.
                    regions.append({'region': region, 'midi_note': int(note),
                                    'mean_cents': float(values.mean()),
                                    'std_cents': float(values.std(unbiased=False))})
                result = {'source_sha256': hashes, 'instrument_id': instrument,
                          'frames': FRAMES, 'seed': seed, 'regions': regions}
                temporary = path.with_suffix('.json.tmp')
                temporary.write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
                temporary.replace(path)
                results.append(result)
                print('Float pitch baseline', instrument, seed, regions, flush=True)
    report = {'conditions': 'Same four-note/rest synthetic scores as closed-loop PTQ; independent float uniform draws; original full learned schedule',
              'limitations': 'Few seeds only, not a statistical interval, perceptual gate, held-out dataset or waveform comparison',
              'cases': results}
    temporary = args.output.with_suffix('.json.tmp')
    temporary.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    temporary.replace(args.output)
    print('Float pitch variability complete', len(results), flush=True)


if __name__ == '__main__':
    main()
