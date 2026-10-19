#!/usr/bin/env python3
# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Import the pinned TFLite into the catalog's PyTorch/ExecuTorch cache format.

Host-only conversion and synthetic validation. Does not build or run firmware.
TensorFlow is used only in the separate --reference-python interpreter.
"""
import argparse
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts"))
from project_config import DEFAULT_SOLUTION, selected_project
from quantized_model import save_quantized_program


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference-python", type=Path, required=True)
    parser.add_argument("--solution", type=Path, default=DEFAULT_SOLUTION)
    args = parser.parse_args()
    import torch
    from model import YoloFastest, INPUT_SHAPE
    from validate import validate
    torch.set_num_threads(4)
    _, solution, project, _, network = selected_project(args.solution, "object_detection")
    model = YoloFastest().eval()
    example = (torch.full(INPUT_SHAPE, model.input_qparams[1], dtype=torch.int8),)
    program = torch.export.export(model, example)
    # The Arm partitioner requires its quantized provenance annotation in
    # addition to Q/DQ. Normally the Arm quantizer emits this metadata; here
    # the parameters came directly from TFLite, with no calibration pass.
    from executorch.backends.arm.common.annotation_meta import ArmAnnotationInfo
    for node in program.graph.nodes:
        if node.op == "call_function":
            node.meta.setdefault("custom", {})[ArmAnnotationInfo.CUSTOM_META_KEY] = {"quantized": True}
    # Check fidelity before accepting new files, then verify the actual cache
    # after the safetensors loader replaces every exported state tensor.
    validate(program.module(), args.reference_python)
    npu = solution["mlops"]["npu"]
    save_quantized_program(program, f"{npu['type'].lower()}-{npu['macs']}", project.parent, network)
    from validate import main as validate_main
    validate_main(["--reference-python", str(args.reference_python), "--solution", str(args.solution)])


if __name__ == "__main__":
    main()
