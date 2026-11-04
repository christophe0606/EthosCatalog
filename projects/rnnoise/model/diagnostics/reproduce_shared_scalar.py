#!/usr/bin/env python3
# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Reproduce the Arm backend shared-constant failure without changing AI assets.

Uses the frozen importer before its broadcast-constant workaround and the
current guarded quantization annotator. Affected ExecuTorch versions raise
KeyError('constant_14') in FuseEqualPlaceholdersPass. This runs on the host;
it does not build firmware, access hardware, or overwrite the deployed model.
"""
from pathlib import Path
import importlib.util
import sys

ROOT = Path(__file__).resolve().parents[4]
MODEL = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
sys.path.insert(0, str(MODEL))

import torch
from create_ai_layer import export_model
from project_config import DEFAULT_SOLUTION, selected_project
from quantized_model import compile_spec
from quantize import annotate_prequantized


def main():
    torch.set_num_threads(1)
    snapshot = Path(__file__).with_name('shared_scalar_importer.py')
    module_spec = importlib.util.spec_from_file_location('rnnoise_shared_scalar', snapshot)
    module = importlib.util.module_from_spec(module_spec)
    module_spec.loader.exec_module(module)
    model = module.RNNoise(source=MODEL / 'upstream/rnnoise_INT8.tflite').eval()
    inputs = tuple(torch.full(shape, model.params[i][1], dtype=torch.int8)
                   for shape, i in zip(module.INPUT_SHAPES, model.inputs))
    program = torch.export.export(model, inputs)
    annotate_prequantized(program)
    _, solution, project, _, network = selected_project(DEFAULT_SOLUTION, 'rnnoise')
    spec = compile_spec(solution, project.parent, network)
    pte, delegates = export_model(program, spec)
    print(f'Compilation succeeded: {len(pte)} bytes, {delegates} delegates; '
          'the recorded failure did not reproduce in this environment.')


if __name__ == '__main__':
    main()
