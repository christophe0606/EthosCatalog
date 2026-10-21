#!/usr/bin/env python3
# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Generate isolated first-text-token checks from the final saved PTQ cache.

These diagnose target execution before autoregressive errors can accumulate.
They do not establish speech quality or upstream implementation parity.
"""
from __future__ import annotations

import json
from pathlib import Path
import sys

import numpy as np
import torch
from safetensors.torch import save_file

LOCAL = Path(__file__).resolve().parent
PROJECT = LOCAL.parent
ROOT = PROJECT.parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from quantized_model import load_quantized_program
from benchmark_fixtures import floats, sha256


class SmallIntermediateTrace(torch.fx.Interpreter):
    """Keep only small first-token operations, excluding weights and KV stacks."""
    def __init__(self, module):
        super().__init__(module)
        self.tensors = {}
        self.operations = []

    def run_node(self, node):
        value = super().run_node(node)
        if (node.op == "call_function" and isinstance(value, torch.Tensor)
                and 0 < value.numel() <= 16384):
            self.tensors[node.name] = value.detach().contiguous().clone()
            self.operations.append(dict(name=node.name, target=str(node.target),
                                        shape=list(value.shape), dtype=str(value.dtype)))
        return value


def main():
    torch.set_num_threads(4)
    manifest_path = LOCAL / "method_manifest.json"
    manifest = json.loads(manifest_path.read_text())
    fixture = json.loads((LOCAL / "benchmark_fixtures.json").read_text())
    if fixture["provenance"]["method_manifest_sha256"] != sha256(manifest_path):
        raise ValueError("Benchmark fixture does not bind the current method manifest")
    methods = {}
    for name in ("backbone", "text_embedding"):
        settings = manifest["methods"][name]
        for key in ("graph", "safetensors"):
            if sha256(PROJECT / settings[key]) != settings[key + "_sha256"]:
                raise ValueError("Method cache differs from conversion manifest")
        methods[name] = load_quantized_program(manifest["target"], PROJECT, settings).module(check_guards=False)
    startup = manifest["startup"]["voice_prefix"]
    prefix_path = PROJECT / startup["raw_file"]
    if sha256(prefix_path) != startup["raw_sha256"]:
        raise ValueError("Voice prefix differs from conversion manifest")
    prefix = torch.from_numpy(np.frombuffer(prefix_path.read_bytes(), dtype="<f4").copy()).reshape(startup["shape"])
    rows = []
    with torch.no_grad():
        for utterance in fixture["utterances"]:
            token = utterance["token_ids"][0]
            embedding = methods["text_embedding"](torch.tensor([[token]], dtype=torch.int32))
            inputs = (embedding, prefix.clone(), torch.tensor(float(startup["position"])))
            hidden, cache = methods["backbone"](*inputs)
            if not rows:
                trace = SmallIntermediateTrace(methods["backbone"])
                traced_hidden, traced_cache = trace.run(*inputs)
                if not torch.equal(hidden, traced_hidden) or not torch.equal(cache, traced_cache):
                    raise ValueError("Tracing changed first-token execution")
                trace_path = LOCAL / "weights/first_token_trace.safetensors"
                save_file(trace.tensors, trace_path,
                          metadata={"method_manifest_sha256": sha256(manifest_path),
                                    "reference": "first token saved pre-Vela PTQ backbone"})
                first_linear = next(node for node in methods["backbone"].graph.nodes
                                    if node.op == "call_function" and str(node.target) == "aten.linear.default")
                input_quant = first_linear.args[0].args[0]
                output_quant = next(node for node in first_linear.users
                                    if str(node.target) == "quantized_decomposed.quantize_per_tensor.default")
                output_dequant = next(node for node in output_quant.users
                                      if str(node.target) == "quantized_decomposed.dequantize_per_tensor.default")
                delegate_nodes = dict(input_float=input_quant.args[0], input_int16=input_quant,
                                      output_int16=output_quant, output_float=output_dequant)
            rows.append(dict(text=utterance["text"], token=token, position=startup["position"],
                             embedding=embedding.flatten().tolist(), hidden=hidden.flatten().tolist(),
                             cache_last_row=cache[..., -1:, :].flatten().tolist()))
    report = dict(format="phonon-first-token-reference-v1", quality_accepted=False,
                  reference="saved pre-Vela PTQ graphs with final PTQ voice prefix",
                  provenance=fixture["provenance"], cache_last_row_shape=[6, 2, 1, 8, 1, 64], cases=rows)
    report["first_case_intermediates"] = dict(file=trace_path.relative_to(PROJECT).as_posix(),
                                             sha256=sha256(trace_path), operations=trace.operations)
    report["first_qkv_linear"] = dict(nodes={key: node.name for key, node in delegate_nodes.items()},
                                     input_quantization=[str(value) for value in input_quant.args[1:]],
                                     output_quantization=[str(value) for value in output_quant.args[1:]])
    (LOCAL / "first_token_reference.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    lines = ["// Generated by model/first_token_reference.py; do not edit.",
             "// Copyright 2026 Arm Limited and/or its affiliates.", "// SPDX-License-Identifier: Apache-2.0",
             "#pragma once", "#include <stdint.h>", "namespace phonon_first_token {"]
    for index, row in enumerate(rows):
        lines += [f"constexpr int32_t token_{index} = {row['token']};",
                  f"constexpr float position_{index} = {row['position']}.0f;"]
        for key in ("embedding", "hidden", "cache_last_row"):
            values = row[key]
            lines += [f"static const float {key}_{index}[{len(values)}] = {{", floats(values), "};"]
    for key, node in delegate_nodes.items():
        tensor = trace.tensors[node.name]
        if tensor.dtype == torch.int16:
            values = tensor.flatten().tolist()
            body = ",\n".join("    " + ", ".join(map(str, values[i:i + 16]))
                              for i in range(0, len(values), 16))
            ctype = "int16_t"
        else:
            body = floats(tensor.numpy())
            ctype = "float"
        lines += [f"static const {ctype} first_qkv_{key}[{tensor.numel()}] = {{", body, "};"]
    lines += ["} // namespace phonon_first_token", ""]
    (LOCAL / "first_token_reference.h").write_text("\n".join(lines), encoding="utf-8")
    print("Generated isolated first-token references for", [row["token"] for row in rows])


if __name__ == "__main__":
    main()
