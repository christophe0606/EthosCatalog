#!/usr/bin/env python3
# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Lower saved pre-Vela methods and report actual delegate/CPU graph coverage."""
from __future__ import annotations
import argparse
from collections import Counter
import contextlib
import json
from pathlib import Path
import sys
import tempfile

import torch
import yaml

LOCAL = Path(__file__).resolve().parent
ROOT = LOCAL.parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from create_ai_layer import export_model
from quantized_model import compile_spec, load_quantized_program, sha256
from export_stages import inspect_pte


def shape(value):
    val = value.meta.get("val") if isinstance(value, torch.fx.Node) else value
    return tuple(int(d) for d in val.shape) if isinstance(val, torch.Tensor) else None


def expensive(node):
    """Dense multiply-accumulate estimates; excludes elementwise/state traffic."""
    target, args = str(node.target), node.args
    out = shape(node)
    if not out:
        return None
    if any(x in target for x in ("linear.", "mm.", "bmm.", "addmm.")):
        lhs = args[1] if "addmm." in target else args[0]
        return dict(operator=target, output_shape=out,
                    estimated_macs=int(torch.tensor(out).prod()) * shape(lhs)[-1])
    if any(x in target for x in ("convolution.", "conv1d.", "conv2d.", "conv_transpose")):
        ins, weight = shape(args[0]), shape(args[1])
        transpose = bool(args[6]) if "convolution." in target else "conv_transpose" in target
        count = int(torch.tensor(ins if transpose else out).prod())
        return dict(operator=target, output_shape=out,
                    estimated_macs=count * weight[1] * int(torch.tensor(weight[2:]).prod()),
                    transposed=transpose)
    return None


def graph_summary(program):
    operators = Counter(str(n.target) for n in program.graph.nodes if n.op == "call_function")
    rows = [row for n in program.graph.nodes if n.op == "call_function" and (row := expensive(n))]
    return dict(operator_counts=dict(sorted(operators.items())), expensive_operations=rows,
                estimated_macs=sum(row["estimated_macs"] for row in rows))


def inspect_edge(edge):
    result = {}
    for name in edge.methods:
        program = edge.exported_program(name)
        cpu = graph_summary(program)
        delegated = []
        for node in program.graph.nodes:
            if node.op == "get_attr":
                module = program.graph_module.get_submodule(node.target)
                if hasattr(module, "original_module"):
                    summary = graph_summary(module.original_module)
                    summary["name"] = node.target
                    summary["delegate_bytes"] = len(module.processed_bytes)
                    delegated.append(summary)
        npu_macs = sum(row["estimated_macs"] for row in delegated)
        result[name] = dict(cpu=cpu, delegates=delegated, delegated_estimated_macs=npu_macs,
                            expensive_macs_delegate_fraction=npu_macs / max(1, npu_macs + cpu["estimated_macs"]))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--memory-mode", choices=["Shared_Sram", "Dedicated_Sram"], default="Shared_Sram")
    args = parser.parse_args()
    torch.set_num_threads(4)
    output = LOCAL / "artifacts" / "staged"; output.mkdir(parents=True, exist_ok=True)
    temp = output / "tmp"; temp.mkdir(exist_ok=True); tempfile.tempdir = str(temp.resolve())
    network = yaml.safe_load((LOCAL / "network.yml").read_text())["network"]
    network["vela"]["memory_mode"] = args.memory_mode
    spec = compile_spec(yaml.safe_load((ROOT / "EthosCatalog.csolution.yml").read_text())["solution"], LOCAL.parent, network)
    spec.dump_intermediate_artifacts_to(str(output / args.memory_mode))
    methods = {name: load_quantized_program(spec.target, LOCAL.parent,
        dict(network, graph=f"model/{name}_quantized.pt2", safetensors=f"model/{name}_quantized.safetensors"))
        for name in ("backbone", "synthesize", "text_embedding")}
    report = dict(memory_mode=args.memory_mode, quality_accepted=False, hardware_realtime_verified=False)
    def capture(edge):
        report["coverage"] = inspect_edge(edge)
    with (output / f"coverage_{args.memory_mode}.log").open("w", encoding="utf-8") as log, contextlib.redirect_stdout(log):
        pte, delegates = export_model(methods, spec, cpu_methods=("text_embedding",), inspection_callback=capture)
    path = output / f"phonon_{args.memory_mode}.pte"; path.write_bytes(pte)
    report.update(pte_bytes=len(pte), pte_sha256=sha256(path), delegates=delegates,
                  serialized_program=inspect_pte(path))
    (LOCAL / f"lowering_{args.memory_mode}.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"[staged] {args.memory_mode}: {len(pte)} bytes, {delegates} delegates", flush=True)


if __name__ == "__main__":
    main()
