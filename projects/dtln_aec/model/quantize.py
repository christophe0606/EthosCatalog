# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Reconstruct and PTQ the published stages with deterministic synthetic audio."""
import argparse
import hashlib
import json
import sys
from pathlib import Path
import numpy as np
import torch
import yaml
from safetensors.torch import save

ROOT = Path(__file__).resolve().parents[3]
MODEL = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'scripts'))
from project_config import DEFAULT_SOLUTION, selected_project
from quantized_model import compile_spec, method_configs, save_quantized_program
from model import DTLNStage


def interpreter(stage):
    import tensorflow as tf
    result = tf.lite.Interpreter(model_path=str(MODEL / 'upstream' / f'dtln_aec_128_{stage}.tflite'),
        experimental_op_resolver_type=tf.lite.experimental.OpResolverType.BUILTIN_WITHOUT_DEFAULT_DELEGATES)
    result.allocate_tensors()
    return result


def extract_weights(stage, reference):
    if stage == 1:
        indices = dict(gamma1=6, beta1=7, gamma2=8, beta2=9, wi1=19, wh1=20,
            b1=10, wi2=17, wh2=18, b2=11, output_weight=16, output_bias=3)
    else:
        indices = dict(gamma1=5, beta1=6, gamma2=7, beta2=8, wi1=18, wh1=19,
            b1=9, wi2=16, wh2=17, b2=10, output_weight=15, output_bias=3,
            encoder=20, decoder=22)
    weights = {name: torch.from_numpy(reference.get_tensor(index).copy()) for name, index in indices.items()}
    if stage == 2:
        weights['encoder'] = weights['encoder'][:, 0, 0, :].contiguous()
        weights['decoder'] = weights['decoder'][:, 0, 0, :].contiguous()
    path = MODEL / f'weights_{stage}.safetensors'
    data = save(weights)
    if not path.is_file() or path.read_bytes() != data:
        path.write_bytes(data)


def reference_forward(reference, inputs):
    for detail, value in zip(reference.get_input_details(), inputs):
        reference.set_tensor(detail['index'], value.detach().numpy())
    reference.invoke()
    return tuple(torch.from_numpy(reference.get_tensor(d['index']).copy()) for d in reference.get_output_details())


def audio_frames(seed, count):
    """Silence, echo-only, near-end-only and double-talk with two echo delays."""
    rng = np.random.default_rng(seed)
    length = count * 128
    time = np.arange(length + 160) / 16000
    far = (.15 * np.sin(2*np.pi*(211*time + 19*time*time)) + .06*np.sin(2*np.pi*733*time))
    near = .12 * np.sin(2*np.pi*173*time) * (.6+.4*np.sin(2*np.pi*3*time))
    near += .025 * rng.normal(size=len(time))
    mode = (np.arange(len(time)) // (128 * 24)) % 4
    far = np.where((mode == 0) | (mode == 2), 0, far)
    near = np.where((mode == 0) | (mode == 1), 0, near)
    mic = near + .65*np.roll(far, 53) + .2*np.roll(far, 113)
    mic[:113] = near[:113]
    mic_window = np.zeros(512, np.float32)
    ref_window = mic_window.copy()
    for frame in range(count):
        mic_window[:-128] = mic_window[128:]
        ref_window[:-128] = ref_window[128:]
        mic_window[-128:] = mic[frame*128:frame*128+128]
        ref_window[-128:] = far[frame*128:frame*128+128]
        yield mic_window.copy(), ref_window.copy()


def calibration_frames():
    """Exercise recurrent state over consecutive, varied acoustic conditions."""
    rng = np.random.default_rng(578)
    sample_rate = 16000
    time = np.arange(16 * sample_rate) / sample_rate
    near_phase = np.cumsum((115 + 39 * np.sin(2*np.pi*.71*time)
                            + 19 * np.sin(2*np.pi*.22*time)) / sample_rate)
    far_phase = np.cumsum((157 + 108 * np.sin(2*np.pi*.57*time)
                           + 34 * np.sin(2*np.pi*.77*time)) / sample_rate)
    near = .2*np.sin(2*np.pi*near_phase) + .07*np.sin(6*np.pi*near_phase) + .012*rng.normal(size=len(time))
    far = .22*np.sin(2*np.pi*far_phase) + .07*np.sin(10*np.pi*far_phase) + .018*rng.normal(size=len(time))
    segment = (time / .73).astype(int)
    mode = segment % 5
    levels = np.linspace(.2, 2.2, 8)
    amplitude = levels[segment % len(levels)]
    near *= (mode >= 2) * (.51 + .49*np.sin(2*np.pi*1.9*time)**2) * amplitude
    far *= ((mode == 1) | (mode == 2)) * (.51 + .49*np.sin(2*np.pi*3.2*time)**2) * amplitude[::-1]
    mic = np.clip(near + .57*np.pad(far, (73, 0))[:-73] + .34*np.pad(far, (146, 0))[:-146], -1, 1)
    pair = np.pad(np.stack((mic, far)).astype(np.float32), ((0, 0), (384, 0)))
    for offset in range(0, len(time), 128):
        yield pair[:, offset:offset + 512]


def traces(models, windows):
    states = [torch.zeros(1, 2, 128, 2), torch.zeros(1, 2, 128, 2)]
    for mic, far in windows:
        spectrum = np.fft.rfft(mic).astype(np.complex64)
        x1 = (torch.from_numpy(np.abs(spectrum)).reshape(1, 1, 257), states[0],
              torch.from_numpy(np.abs(np.fft.rfft(far)).astype(np.float32)).reshape(1, 1, 257))
        mask, states[0] = models[0](*x1)
        estimated = np.fft.irfft(spectrum * mask.detach().numpy().reshape(-1), n=512).astype(np.float32)
        x2 = (torch.from_numpy(estimated).reshape(1, 1, 512), states[1], torch.from_numpy(far).reshape(1, 1, 512))
        output, states[1] = models[1](*x2)
        yield x1, x2, output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--solution', type=Path, default=DEFAULT_SOLUTION)
    parser.add_argument('--project', default='dtln_aec')
    args = parser.parse_args()
    _, solution, project, config, network = selected_project(args.solution, args.project)
    assert project.parent == MODEL.parent
    torch.set_num_threads(1)
    references = [interpreter(s) for s in (1, 2)]
    for stage, reference in enumerate(references, 1):
        extract_weights(stage, reference)
    models = [DTLNStage(s).eval() for s in (1, 2)]
    from executorch.backends.arm.quantizer import EthosUQuantizer, get_symmetric_a16w8_quantization_config
    from torchao.quantization.pt2e.quantize_pt2e import prepare_pt2e, convert_pt2e
    from torchao.quantization.pt2e.observer import MinMaxObserver
    from dataclasses import replace
    spec = compile_spec(solution, project.parent, network)
    examples = [(torch.ones(1, 1, width)*.01, torch.zeros(1, 2, 128, 2), torch.ones(1, 1, width)*.02) for width in (257, 512)]
    qconfig = get_symmetric_a16w8_quantization_config(epsilon=2**-24)
    activation = replace(qconfig.input_activation, observer_or_fake_quant_ctr=MinMaxObserver.with_args(eps=2**-24))
    qconfig = replace(qconfig, input_activation=activation, output_activation=activation)
    # Keep the LSTM updates and packed hidden/cell state in float32. The cell
    # values can be far larger than the hidden values, so quantizing the whole
    # state with one activation scale sacrifices precision on every hop.
    # Quantizing before log-power also loses low-energy spectral bins.
    prepared = [
        prepare_pt2e(
            torch.export.export(model, inputs).module(check_guards=False),
            EthosUQuantizer(spec).set_global(None).set_node_target(torch.ops.aten.linear.default, qconfig),
        )
        for model, inputs in zip(models, examples)
    ]
    parity = [0., 0.]
    silence_parity = [0., 0.]
    with torch.no_grad():
        for trace in traces(models, audio_frames(310, 192)):
            for s in (0, 1):
                inputs = trace[s]
                actual = models[s](*inputs)
                expected = reference_forward(references[s], inputs)
                target = parity if inputs[0].abs().max() > 0 and inputs[2].abs().max() > 0 else silence_parity
                target[s] = max(target[s], *(float((a-b).abs().max()) for a, b in zip(actual, expected)))
        for calibration_count, trace in enumerate(traces(models, calibration_frames()), 1):
            for stage, module in enumerate(prepared):
                module(*trace[stage])
    assert max(parity) < 2e-4, parity
    quantized = [convert_pt2e(p, fold_quantize=True) for p in prepared]
    errors = [[], []]
    state_errors = [[], []]
    fixture = {}
    qstates = [torch.zeros(1, 2, 128, 2), torch.zeros(1, 2, 128, 2)]
    with torch.no_grad():
        for frame, trace in enumerate(traces(models, audio_frames(901, 128))):
            for s in (0, 1):
                inputs = trace[s]
                expected = models[s](*inputs)
                qinputs = (inputs[0], qstates[s], inputs[2])
                out, qstates[s] = quantized[s](*qinputs)
                errors[s].extend((out-expected[0]).abs().flatten().tolist())
                state_errors[s].extend((qstates[s]-expected[1]).abs().flatten().tolist())
                if frame == 40:
                    for n, value in enumerate(inputs): fixture[f'stage{s+1}_input{n}'] = value.numpy()
                    for n, value in enumerate(quantized[s](*inputs)): fixture[f'stage{s+1}_output{n}'] = value.numpy()
    np.savez(MODEL / 'method_fixtures.npz', **fixture)
    header = ['// Copyright 2026 Arm Limited and/or its affiliates.',
              '// SPDX-License-Identifier: Apache-2.0', '#pragma once']
    for name, value in fixture.items():
        values = value.reshape(-1)
        header.append(f'static const float {name}[{len(values)}] = {{')
        for start in range(0, len(values), 8):
            header.append('    ' + ', '.join(f'{float(v):.9e}f' for v in values[start:start+8]) + ',')
        header.append('};')
    (MODEL / 'model_test_data.h').write_text('\n'.join(header)+'\n')
    report = dict(calibration_frames=calibration_count, evaluation_frames=128, float_shared_state_nonzero_max_absolute_error=parity,
        float_shared_state_zero_input_max_absolute_error=silence_parity,
        activation='int16 at linear operators; spectral features, nonlinearities and recurrent state updates float32',
        weights='per-channel int8 for linear operators',
        data=('16 seconds of continuous synthetic near/far audio with varying signal levels, frequency, two echo '
              'delays and silence, echo-only, near-only and double-talk transitions; calibration seed 578 and '
              'held-out method evaluation seed 901'),
        limitation='Synthetic numeric conversion checks do not establish speech quality or AEC challenge scores.')
    for s in (0, 1):
        report[f'stage{s+1}'] = dict(output_mae=float(np.mean(errors[s])), output_max_absolute_error=max(errors[s]),
            state_mae=float(np.mean(state_errors[s])), state_max_absolute_error=max(state_errors[s]))
    document = yaml.safe_load(config.read_text())
    document['network']['quantization']['error'] = report
    config.write_text(
        "# Copyright 2026 Arm Limited and/or its affiliates.\n"
        "# SPDX-License-Identifier: Apache-2.0\n"
        + yaml.safe_dump(document, sort_keys=False),
        encoding="utf-8",
        newline="\n",
    )
    (MODEL / 'conversion_metrics.json').write_text(json.dumps(report, indent=2)+'\n')
    for (name, settings), model, example in zip(method_configs(network).items(), quantized, examples):
        save_quantized_program(torch.export.export(model, example), spec.target, project.parent, settings)
    print(json.dumps(report, indent=2), flush=True)


if __name__ == '__main__':
    main()
