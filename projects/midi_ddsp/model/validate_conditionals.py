# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Separate held-state PTQ error from sampled closed-loop divergence."""
import argparse
import hashlib
import json
import sys
from pathlib import Path
import numpy as np
import torch
import yaml
import torchao.quantization.pt2e.quantize_pt2e  # Register saved quantized ops.
import executorch.exir.passes.dim_order_ops_registry  # Register Arm layout ops.
from model import load_model
from deployment import methods, render_controls, score, FRAMES, load_saved_method
from quantize_deployment import metric

HERE = Path(__file__).resolve().parent


def top_p(logits):
    sorted_logits = logits.sort(descending=True).values
    probability = sorted_logits.softmax(-1)
    threshold = torch.where(probability.cumsum(-1)-probability < .95,
                            sorted_logits, torch.full_like(logits, 1000.)).amin(-1, keepdim=True)
    return torch.where(logits < threshold, torch.full_like(logits, -1e10), logits).softmax(-1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--suite', choices=('four_regions', 'demo_phrase'), default='four_regions')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if args.suite == 'demo_phrase':
        from score_fixtures import demo_score
        fixture = demo_score
    else:
        fixture = score
    torch.set_num_threads(1)
    model = load_model(); ops = methods(model)
    quantized = {name: load_saved_method(name,HERE/f'{name}_quantized.pt2') for name in ops}
    cases = []
    with torch.no_grad():
        for instrument in range(13):
            capture = {name: [] for name in ops}
            reference = render_controls(model, ops, *fixture(instrument, FRAMES, 9100+instrument), capture=capture)
            local = {}; conditional = []
            for name in ops:
                values = capture[name]
                indices = np.linspace(0, len(values)-1, min(len(values), 128 if name == 'pitch_step' else 16)).astype(int)
                expected = []; actual = []
                for index in indices:
                    x = values[index]
                    expected.append(ops[name](x)); actual.append(quantized[name](x))
                expected = torch.cat(expected); actual = torch.cat(actual)
                local[name] = metric(actual, expected)
                if name == 'expression_step':
                    local[name]['expression_controls'] = metric(actual[:, :6], expected[:, :6])
                if name == 'harmonic':
                    local[name]['amplitude'] = metric(actual[..., :1], expected[..., :1])
                    local[name]['harmonics'] = metric(actual[..., 1:61], expected[..., 1:61])
                    local[name]['noise'] = metric(actual[..., 61:], expected[..., 61:])
                if name == 'pitch_step':
                    logits = expected[:, :201]; candidate = actual[:, :201]
                    lp = logits.log_softmax(-1); lq = candidate.log_softmax(-1)
                    p = top_p(logits); q = top_p(candidate)
                    cents = torch.arange(201).float()-100.
                    conditional = {
                        'held_float_states_context_and_previous_samples': True,
                        'frames_sampled': len(indices),
                        'logits': metric(candidate, logits),
                        'hidden_states': metric(actual[:,201:], expected[:,201:]),
                        'mean_untruncated_kl_float_to_ptq': float((lp.exp()*(lp-lq)).sum(-1).mean()),
                        'mean_untruncated_total_variation': float((lp.exp()-lq.exp()).abs().sum(-1).mean()/2),
                        'mean_original_top_p_total_variation': float((p-q).abs().sum(-1).mean()/2),
                        'mean_absolute_expected_pitch_shift_cents': float(((p-q)*cents).sum(-1).abs().mean()),
                        'maximum_absolute_expected_pitch_shift_cents': float(((p-q)*cents).sum(-1).abs().max())}
            cases.append({'instrument_id': instrument, 'frames': FRAMES, 'local_method_metrics': local,
                          'pitch_conditionals': conditional})
            print('Teacher-forced instrument', instrument, conditional, flush=True)
    gate = {'mean_original_top_p_tv_maximum': .01,
            'mean_absolute_expected_pitch_shift_cents_maximum': .1,
            'local_harmonic_rmse_over_reference_rms_maximum': .01}
    passed = all(c['pitch_conditionals']['mean_original_top_p_total_variation'] <= gate['mean_original_top_p_tv_maximum']
                 and c['pitch_conditionals']['mean_absolute_expected_pitch_shift_cents'] <= gate['mean_absolute_expected_pitch_shift_cents_maximum']
                 and c['local_method_metrics']['harmonic']['rmse']/c['local_method_metrics']['harmonic']['reference_rms'] <= gate['local_harmonic_rmse_over_reference_rms_maximum']
                 for c in cases)
    report = {'preprocessing': 'Corrected upstream synthesis clip and rest relative position; raw expression feedback retained',
              'suite': args.suite,
              'conditions': '13 synthetic1000frame segments, same held float states/context/previous categorical sample;128 well-spaced pitch frames each;up to16 inputs other methods,full harmonic block',
              'purpose': 'Measures local conditional quantization error separately from independent accumulated sampled trajectories; not a perceptual or waveform acceptance test',
              'synthetic_local_benchmark_gate': gate, 'passed_local_benchmark_gate': passed,
              'cases': cases}
    files = [HERE/name for name in ('model.py', 'deployment.py', 'pretrained.safetensors', 'validate_conditionals.py')]
    files += [HERE/f'{name}_quantized.{extension}' for name in ops for extension in ('pt2', 'safetensors')]
    if args.suite == 'demo_phrase':
        files += [HERE/'score_fixtures.py', HERE.parent/'player/score_features.py', HERE.parent/'player/make_demo_midi.py']
    report['file_sha256'] = {path.relative_to(HERE.parent).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest() for path in files}
    output = args.output or HERE/('demo_conditionals.json' if args.suite == 'demo_phrase' else 'deployment_conditionals.json')
    output.write_text(json.dumps(report, indent=2)+'\n')
    print('Local gate', args.suite, 'PASS' if passed else 'FAIL', output, flush=True)
    raise SystemExit(0 if passed else 1)


if __name__ == '__main__':
    main()
