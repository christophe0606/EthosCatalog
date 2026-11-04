#!/usr/bin/env python3
# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Import original INT8 graph; no training or new quantization."""
import argparse
import json
from pathlib import Path
import sys
import torch
ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'scripts'))
from project_config import DEFAULT_SOLUTION, selected_project
from quantized_model import compile_spec, save_quantized_program
from model import RNNoise, INPUT_SHAPES


def annotate_prequantized(program):
    """Identify actual source Q/DQ regions without recalibrating any values.

    The Arm backend requires an explicit annotation on floating ATen compute
    enclosed by Q/DQ, even when all quantization is already present. Validate
    both sides of each such operation before using its official marker.
    """
    from collections import Counter
    from executorch.backends.arm.quantizer.arm_quantizer_utils import mark_node_as_annotated
    qops = {torch.ops.quantized_decomposed.quantize_per_tensor.default,
            torch.ops.quantized_decomposed.quantize_per_channel.default}
    dqops = {torch.ops.quantized_decomposed.dequantize_per_tensor.default,
             torch.ops.quantized_decomposed.dequantize_per_channel.default}
    relu = torch.ops.aten.relu.default
    compute = {torch.ops.aten.conv2d.default, torch.ops.aten.add.Tensor,
               torch.ops.aten.sub.Tensor, torch.ops.aten.mul.Tensor,
               torch.ops.aten.tanh.default, torch.ops.aten.sigmoid.default}
    counts = Counter()
    for node in program.graph.nodes:
        if node.op != 'call_function':
            continue
        if node.target in compute:
            assert node.all_input_nodes and all(n.target in dqops for n in node.all_input_nodes), node
            assert node.users and all(n.target in qops or (n.target == relu and n.users and all(u.target in qops for u in n.users)) for n in node.users), node
            mark_node_as_annotated(node)
            counts[str(node.target)] += 1
        elif node.target == relu:
            assert len(node.all_input_nodes) == 1 and node.all_input_nodes[0].target in compute, node
            assert node.users and all(n.target in qops for n in node.users), node
            mark_node_as_annotated(node)
            counts[str(node.target)] += 1
        elif node.target not in qops | dqops:
            value = node.meta.get('val')
            values = value if isinstance(value, (list, tuple)) else [value]
            assert all(not isinstance(v, torch.Tensor) or not v.dtype.is_floating_point for v in values), node
            mark_node_as_annotated(node)
            counts['integer_structure'] += 1
    return dict(counts)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--solution', type=Path, default=DEFAULT_SOLUTION)
    parser.add_argument('--project', default='rnnoise')
    args = parser.parse_args()
    _, solution, project, _, network = selected_project(args.solution, args.project)
    assert project.parent == Path(__file__).resolve().parents[1]
    torch.set_num_threads(1)
    model = RNNoise().eval()
    inputs = tuple(torch.full(shape, model.params[i][1], dtype=torch.int8) for shape, i in zip(INPUT_SHAPES, model.inputs))
    program = torch.export.export(model, inputs)
    annotations = annotate_prequantized(program)
    (project.parent / 'model/conversion_annotations.json').write_text(json.dumps(annotations, indent=2) + '\n')
    spec = compile_spec(solution, project.parent, network)
    save_quantized_program(program, spec.target, project.parent, network)
    print('Original RNNoise int8 weights/scales imported; no PTQ performed.', flush=True)

if __name__ == '__main__': main()
