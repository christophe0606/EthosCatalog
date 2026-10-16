#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Compare the converted graph against original TFLite on synthetic inputs."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

import numpy as np
import torch

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts"))
from project_config import DEFAULT_SOLUTION, selected_project
from quantized_model import load_quantized_program, sha256
from fake_inputs import SEED, fake_inputs
from model import SOURCE, SOURCE_SHA256, MicroNet


def validate(module, reference_python):
    names, inputs = fake_inputs()
    (ROOT / "tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="micronet-", dir=ROOT / "tmp") as directory:
        temp = Path(directory)
        np.save(temp / "inputs.npy", inputs)
        env = dict(os.environ, TF_CPP_MIN_LOG_LEVEL="2")
        subprocess.run([str(reference_python.resolve()), str(Path(__file__).with_name("tflite_reference.py")),
                        "--source", str(SOURCE), "--inputs", str(temp / "inputs.npy"),
                        "--outputs", str(temp / "outputs.npz")], check=True, env=env)
        with np.load(temp / "outputs.npz", allow_pickle=False) as output:
            tf_version = str(output["tensorflow_version"])
            references = {key: output[key].copy() for key in ("reference", "default")}
    with torch.no_grad():
        actual = np.stack([module(torch.from_numpy(x)).numpy() for x in inputs])
    assert actual.dtype == np.int8 and actual.shape == (len(inputs), 1, 12)
    metrics = {}
    for mode, expected in references.items():
        errors = np.abs(actual.astype(np.int32) - expected.astype(np.int32))
        agreement = np.argmax(actual, axis=-1) == np.argmax(expected, axis=-1)
        worst = int(np.argmax(errors.max(axis=(1, 2))))
        metrics[mode] = {
            "max_error_lsb": int(errors.max()), "mean_error_lsb": float(errors.mean()),
            "exact_output_fraction": float((errors == 0).mean()),
            "top1_agreement": float(agreement.mean()), "worst_pattern": names[worst],
            "top1_differences": [names[i] for i in np.flatnonzero(~agreement.reshape(-1))],
        }
        # At most two output quantization steps, including accumulated fixed
        # point vs floating Q/DQ rounding across all 16 convolutions.
        if errors.max() > (1 if mode == "default" else 2):
            raise AssertionError(f"Conversion differs from TFLite {mode}: {metrics[mode]}")
        if mode == "default" and not agreement.all():
            raise AssertionError("Converted top-1 scores differ from default TFLite")
    report = {"source_sha256": SOURCE_SHA256, "seed": SEED, "patterns": len(inputs),
              "input_shape": [1, 49, 10, 1], "input_dtype": "int8",
              "output_shape": [1, 12], "output_dtype": "int8",
              "max_allowed_error_lsb": {"reference": 2, "default": 1}, "comparisons": metrics,
              "versions": {"torch": torch.__version__, "tensorflow": tf_version},
              "scope": "Host exported PyTorch Q/DQ graph vs TFLite; no firmware or NPU execution"}
    print(json.dumps(report, indent=2), flush=True)
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference-python", type=Path, required=True)
    parser.add_argument("--solution", type=Path, default=DEFAULT_SOLUTION)
    args = parser.parse_args(argv)
    torch.set_num_threads(4)
    _, solution, project, config, network = selected_project(args.solution, "kws_micronet_l")
    npu = solution["mlops"]["npu"]
    program = load_quantized_program(f"{npu['type'].lower()}-{npu['macs']}", project.parent, network)
    module = program.module()
    report = validate(module, args.reference_python)
    # The exported artifact and safetensors restoration must also reproduce
    # the Python import exactly, independently of the TFLite tolerance.
    original = MicroNet().eval()
    _, inputs = fake_inputs()
    with torch.no_grad():
        for x in inputs:
            tensor = torch.from_numpy(x)
            torch.testing.assert_close(module(tensor), original(tensor), rtol=0, atol=0)
    report["restored_cache_matches_python_exactly"] = True
    report["artifacts"] = {key: sha256(project.parent / network[key])
                           for key in ("model", "quantize", "graph", "safetensors")}
    report["validation_sources"] = {name: sha256(Path(__file__).with_name(name))
                                    for name in ("validate.py", "tflite_reference.py", "fake_inputs.py")}
    Path(__file__).with_name("validation.json").write_text(json.dumps(report, indent=2)+"\n", encoding="utf-8")


if __name__ == "__main__":
    main()
