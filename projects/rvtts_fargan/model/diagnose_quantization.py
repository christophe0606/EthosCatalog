#!/usr/bin/env python3
# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Locate saturation and state errors on held-out source feature trajectories."""
import json
import sys
import numpy as np
import torch
import yaml
import executorch.exir  # Register dimension-order operators in saved Arm graphs.
from model import HERE, Fargan, FarganInit
from quantize import corrected, trace, SHARED
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from quantized_model import load_quantized_program, method_configs


class Saturation(torch.fx.Interpreter):
    def __init__(self, model):
        super().__init__(model)
        self.current = None
        self.saturation = []

    def run_node(self, node):
        self.current = node
        return super().run_node(node)

    def call_function(self, target, args, kwargs):
        if target == torch.ops.quantized_decomposed.quantize_per_tensor.default:
            value, scale, zero, qmin, qmax, _ = args
            low, high = (qmin-zero)*scale, (qmax-zero)*scale
            clipped = (value < low) | (value > high)
            if clipped.any():
                self.saturation.append({"node": self.current.name, "values": int(value.numel()),
                    "clipped": int(clipped.sum()), "range": [float(value.min()), float(value.max())],
                    "quantized_range": [low, high]})
        return super().call_function(target, args, kwargs)


def main():
    torch.set_num_threads(1)
    network = yaml.safe_load((HERE / "network.yml").read_text())["network"]
    models = {"fargan_init": FarganInit().eval(), "fargan_frame": Fargan().eval()}
    quantized = {name: load_quantized_program("ethos-u85-256", HERE.parent, method_configs(network)[name]).module(check_guards=False) for name in models}
    data = np.load(SHARED / "source_fixtures.npz")
    features = corrected(data["case_5_acoustic_reference"])
    captured = {name: [] for name in models}
    with torch.no_grad(): trace(models, features, captured)
    result = {}
    for name, values in captured.items():
        groups = {"excitation": (0, 160), "pitch": (160, 416), "condition": (416, 544),
                  "fwc": (544, 708), "h1": (708, 868), "h2": (868, 996), "h3": (996, 1124)} if name == "fargan_frame" else {"state": (0, 964)}
        max_by_group = {key: 0 for key in groups}
        worst, worst_error = 0, 0
        with torch.no_grad():
            for i, args in enumerate(values):
                error = (models[name](*args) - quantized[name](*args)).abs()
                for key, (a,b) in groups.items():
                    max_by_group[key] = max(max_by_group[key], float(error[:, a:b].max()))
                if float(error.max()) > worst_error:
                    worst, worst_error = i, float(error.max())
        monitor = Saturation(quantized[name])
        with torch.no_grad(): monitor.run(*values[worst])
        result[name] = {"worst_call": worst, "max_error_by_output_group": max_by_group, "saturation_on_worst_call": monitor.saturation}
    (HERE / "quantization_diagnosis.json").write_text(json.dumps(result, indent=2)+"\n")
    print(result, flush=True)


if __name__ == "__main__":
    main()
