# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Compare complete held-out fake-source streams with independent states."""
import json
import sys
from pathlib import Path
import numpy as np
import torch

MODEL = Path(__file__).resolve().parent
ROOT = MODEL.parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from project_config import selected_project
from quantized_model import load_quantized_program, method_configs
from quantize import interpreter, reference_forward
from model import DTLNStage
from test_patterns import patterns, MODES, SAMPLE_RATE


def run(models, microphone, far):
    states = [torch.zeros(1, 2, 128, 2), torch.zeros(1, 2, 128, 2)]
    microphone = np.pad(microphone, (384, 384))
    far = np.pad(far, (384, 384))
    enhanced = np.zeros_like(microphone)
    with torch.no_grad():
        for offset in range(0, microphone.size - 511, 128):
            region = slice(offset, offset + 512)
            spectrum = np.fft.rfft(microphone[region]).astype(np.complex64)
            magnitude = np.abs(spectrum).astype(np.float32)
            far_magnitude = np.abs(np.fft.rfft(far[region])).astype(np.float32)
            mask, states[0] = models[0](torch.from_numpy(magnitude).reshape(1, 1, 257), states[0],
                torch.from_numpy(far_magnitude).reshape(1, 1, 257))
            estimate = np.fft.irfft(spectrum * mask.numpy().reshape(-1), n=512).astype(np.float32)
            output, states[1] = models[1](torch.from_numpy(estimate).reshape(1, 1, 512), states[1],
                torch.from_numpy(far[region]).reshape(1, 1, 512))
            enhanced[region] += output.numpy().reshape(-1)
    return enhanced[384:-384]


def rms(values):
    return float(np.sqrt(np.mean(values.square() if isinstance(values, torch.Tensor) else values**2)))


def main():
    _, _, project, _, network = selected_project(project='dtln_aec')
    torch.set_num_threads(1)
    reference = [interpreter(s) for s in (1, 2)]
    source_models = [lambda *inputs, ref=ref: reference_forward(ref, inputs) for ref in reference]
    quantized = [load_quantized_program('ethos-u85-256', project.parent, settings).module(check_guards=False)
        for settings in method_configs(network).values()]
    mixed, far, clean = patterns()
    outputs = {'tflite': run(source_models, mixed, far), 'float': run([DTLNStage(1), DTLNStage(2)], mixed, far),
        'ptq': run(quantized, mixed, far)}
    assert all(np.isfinite(output).all() for output in outputs.values())
    metrics = {}
    for section, mode in enumerate(MODES):
        region = slice(section * SAMPLE_RATE + 4000, (section + 1) * SAMPLE_RATE - 4000)
        entry = {'input_rms': rms(mixed[region])}
        for name, output in outputs.items():
            entry[name + '_rms'] = rms(output[region])
            entry[name + '_residual_rms'] = rms(output[region] - clean[region])
        entry['ptq_vs_tflite_mae'] = float(np.mean(abs(outputs['ptq'][region] - outputs['tflite'][region])))
        metrics[f'{section}_{mode}'] = entry
    metrics['global'] = {name + '_mae_vs_tflite': float(np.mean(abs(output - outputs['tflite'])))
        for name, output in outputs.items()}
    (MODEL / 'heldout_metrics.json').write_text(json.dumps(metrics, indent=2) + '\n')
    print(json.dumps(metrics, indent=2))


if __name__ == '__main__':
    main()
