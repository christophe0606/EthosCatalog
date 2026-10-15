#!/usr/bin/env python3
# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""MRAM template-owned model loading, calibration data and quantization policy.

Normally called by scripts/create_ai_layer.py. Firmware test data is maintained
separately in this project's model/model_test_data.h and is never written here.
"""
import argparse
import importlib.util
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts"))
from project_config import DEFAULT_SOLUTION, selected_project
from quantized_model import compile_spec, save_quantized_program


def calibration_inputs():
    """Represent the template's NHWC input range and both ReLU branches."""
    import torch
    shape = (1, 8, 8, 1)
    generator = torch.Generator().manual_seed(17)
    return [
        torch.linspace(-1.0, 1.0, 64).reshape(shape),
        torch.full(shape, -1.0),
        torch.full(shape, 1.0),
        torch.rand(shape, generator=generator) * 2.0 - 1.0,
    ]


def quantize_model(spec, project_dir, network):
    import torch
    from executorch.backends.arm.quantizer import EthosUQuantizer, get_symmetric_quantization_config
    from torchao.quantization.pt2e.quantize_pt2e import convert_pt2e, prepare_pt2e

    source = project_dir / network["model"]
    module_spec = importlib.util.spec_from_file_location("template_model", source)
    if module_spec is None or module_spec.loader is None:
        raise ValueError(f"Cannot import the template's Python model: {source}")
    module = importlib.util.module_from_spec(module_spec)
    module_spec.loader.exec_module(module)
    model = module.SmokeModel().eval()
    samples = calibration_inputs()
    example = (samples[0],)
    graph = torch.export.export(model, example).module()
    quantizer = EthosUQuantizer(spec)
    quantizer.set_global(get_symmetric_quantization_config(is_per_channel=True))
    prepared = prepare_pt2e(graph, quantizer)
    with torch.no_grad():
        for sample in samples:
            prepared(sample)
    quantized = convert_pt2e(prepared, fold_quantize=True)
    with torch.no_grad():
        for sample in samples:
            torch.testing.assert_close(quantized(sample), model(sample), atol=0.02, rtol=0)
    program = torch.export.export(quantized, example)
    if not any(value.dtype == torch.int8 for value in program.state_dict.values()):
        raise ValueError("Template quantization produced no int8 weights")
    save_quantized_program(program, spec.target, project_dir, network)
    print(f"[template_mram] calibrated with {len(samples)} project inputs; saved quantized graph/weights before Vela",
          flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--solution", type=Path, default=DEFAULT_SOLUTION)
    parser.add_argument("--project", help="Application name or cproject path; default: CMSIS UI selection")
    args = parser.parse_args()
    _, solution, project, _, network = selected_project(args.solution, args.project)
    if project.parent != Path(__file__).resolve().parents[1]:
        parser.error("This quantizer belongs to its MRAM template project; select that project first")
    quantize_model(compile_spec(solution, project.parent, network), project.parent, network)


if __name__ == "__main__":
    main()
