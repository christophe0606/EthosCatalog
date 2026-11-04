# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Resume closed-loop PTQ validation from the accepted saved method caches.

This never recalibrates, regenerates the PTE, or mutates network.yml. Each
instrument is checkpointed independently, with hashes that prevent stale reuse.
"""
import argparse
import hashlib
import json
from pathlib import Path
import numpy as np
import torch
import torchao.quantization.pt2e.quantize_pt2e  # Register saved quantized ops.
import executorch.exir.passes.dim_order_ops_registry  # Register Arm layout ops.
from model import load_model
from deployment import methods, render_controls, score, FRAMES, load_saved_method
from quantize_deployment import metric

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]


def atomic_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + '.tmp')
    temporary.write_text(json.dumps(value, indent=2)+'\n', encoding='utf-8')
    temporary.replace(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--checkpoint-dir', type=Path, default=ROOT/'.flow/midi_ddsp/closed_loop')
    parser.add_argument('--output', type=Path, default=HERE/'deployment_ptq_validation.json')
    parser.add_argument('--instruments', type=int, nargs='+', default=list(range(13)))
    args = parser.parse_args()
    if any(i < 0 or i > 12 for i in args.instruments):
        parser.error('instrument IDs must be 0..12')
    torch.set_num_threads(1)
    model = load_model()
    ops = methods(model)
    files = [HERE/name for name in ('pretrained.safetensors', 'model.py', 'deployment.py', 'validate_closed_loop.py')]
    files += [HERE/f'{name}_quantized.{extension}' for name in ops for extension in ('pt2', 'safetensors')]
    hashes = {path.name: hashlib.sha256(path.read_bytes()).hexdigest() for path in files}
    provenance = {'file_sha256': hashes, 'frames': FRAMES, 'uniform_seed_base': 9100}
    quantized = {name: load_saved_method(name,HERE/f'{name}_quantized.pt2') for name in ops}
    args.checkpoint_dir.mkdir(parents=True, exist_ok=True)
    with torch.no_grad():
        for instrument in args.instruments:
            checkpoint = args.checkpoint_dir/f'instrument_{instrument:02}.json'
            if checkpoint.exists():
                previous = json.loads(checkpoint.read_text())
                if previous['provenance'] != provenance:
                    raise ValueError(f'Stale checkpoint: {checkpoint}; choose another --checkpoint-dir')
                print('Reusing completed instrument', instrument, flush=True)
                continue
            inputs = score(instrument, FRAMES, 9100+instrument)
            actual = render_controls(model, quantized, *inputs)
            expected = render_controls(model, ops, *inputs)
            lp = expected[3].log_softmax(-1)
            lq = actual[3].log_softmax(-1)
            notes = inputs[0][0]
            mapping = inputs[3][0]
            pitch_stats = []
            for region, note in enumerate(notes):
                selected = mapping == region
                a = actual[1][0, selected, 0] * 100.
                b = expected[1][0, selected, 0] * 100.
                pitch_stats.append({'region': region, 'midi_note': int(note),
                                    'ptq_mean_cents': float(a.mean()), 'float_mean_cents': float(b.mean()),
                                    'ptq_std_cents': float(a.std(unbiased=False)),
                                    'float_std_cents': float(b.std(unbiased=False))})
            result = {'instrument_id': instrument, 'frames': FRAMES,
                      'metrics_raw_pitch_expression_logits': [metric(a, b) for a, b in zip(actual, expected)],
                      'mean_logits_kl_float_to_ptq': float((lp.exp()*(lp-lq)).sum(-1).mean()),
                      'mean_logits_total_variation': float((lp.exp()-lq.exp()).abs().sum(-1).mean()/2),
                      'finite': all(bool(torch.isfinite(x).all()) for x in actual), 'pitch_regions': pitch_stats}
            if not result['finite']:
                raise ValueError(f'Nonfinite outputs: instrument {instrument}')
            np.savez_compressed(args.checkpoint_dir/f'instrument_{instrument:02}_controls.npz',
                                ptq_raw=actual[0].numpy(), float_raw=expected[0].numpy(),
                                ptq_pitch=actual[1].numpy(), float_pitch=expected[1].numpy())
            atomic_json(checkpoint, {'provenance': provenance, 'result': result})
            print('Completed closed-loop instrument', instrument, result, flush=True)
    completed = []
    for instrument in range(13):
        path = args.checkpoint_dir/f'instrument_{instrument:02}.json'
        if path.exists():
            item = json.loads(path.read_text())
            if item['provenance'] != provenance:
                raise ValueError(f'Stale checkpoint: {path}')
            completed.append(item['result'])
    report = {'provenance': provenance, 'complete': len(completed) == 13,
              'scheme': 'a16w8 original learned methods; original float normalization and CPU top-p',
              'preprocessing': 'Clipped synthesis conditioning; raw masked expression feedback; zero rest position',
              'reference': 'Saved imported original weights; independent TF parity and held-state conditional reports alongside',
              'closed_loop': completed,
              'limitations': 'Synthetic scores only. Coupled random paths can diverge after categorical boundary flips. No final audio, seam, perceptual or board acceptance follows.'}
    atomic_json(args.output, report)
    print(f'Closed-loop progress: {len(completed)}/13, report {args.output}', flush=True)


if __name__ == '__main__':
    main()
