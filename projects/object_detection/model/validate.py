#!/usr/bin/env python3
# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Check both int8 heads and decoded sample faces against original TFLite."""
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
from model import YoloFastest, SOURCE, SOURCE_SHA256
from patterns import patterns, SEED
from decode import decode, iou


def reference(inputs, reference_python):
    (ROOT / "tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="detection-reference-", dir=ROOT / "tmp") as directory:
        temp = Path(directory)
        np.save(temp / "inputs.npy", inputs)
        subprocess.run([str(reference_python.resolve()), str(Path(__file__).with_name("tflite_reference.py")),
                        "--source", str(SOURCE), "--inputs", str(temp / "inputs.npy"),
                        "--outputs", str(temp / "outputs.npz")], check=True,
                       env=dict(os.environ, TF_CPP_MIN_LOG_LEVEL="2"))
        with np.load(temp / "outputs.npz", allow_pickle=False) as output:
            return {key: output[key].copy() for key in output.files}


def validate(module, reference_python):
    names, inputs = patterns()
    references = reference(inputs, reference_python)
    with torch.no_grad():
        values = [module(torch.from_numpy(x)) for x in inputs]
    actual = [np.stack([v[i].numpy() for v in values]) for i in range(2)]
    metrics = {}
    baseline = []
    for i in range(2):
        error = np.abs(references[f"reference_{i}"].astype(np.int32)-references[f"default_{i}"].astype(np.int32))
        baseline.append({"max_error_lsb": int(error.max()), "mean_error_lsb": float(error.mean()),
                         "exact_fraction": float((error == 0).mean())})
    for mode in ("reference", "default"):
        heads = []
        for i, value in enumerate(actual):
            error = np.abs(value.astype(np.int32) - references[f"{mode}_{i}"].astype(np.int32))
            heads.append({"max_error_lsb": int(error.max()), "mean_error_lsb": float(error.mean()),
                          "exact_fraction": float((error == 0).mean()),
                          "p99_error_lsb": float(np.percentile(error, 99)),
                          "worst_pattern": names[int(np.argmax(error.reshape(len(names), -1).max(axis=1)))]})
        detection = []
        for index, name in enumerate(names[:5]):
            expected = decode([references[f"{mode}_{i}"][index] for i in range(2)])
            observed = decode([value[index] for value in actual])
            matches = [max((iou(e["box"], o["box"]) for o in observed), default=0) for e in expected]
            detection.append({"pattern": name, "reference": expected, "converted": observed,
                              "matched_ious": matches,
                              "same_count": len(expected) == len(observed)})
        metrics[mode] = {"heads": heads, "sample_detections": detection}
    report = {"source_sha256": SOURCE_SHA256, "seed": SEED, "patterns": len(names),
              "names": names, "comparisons": metrics,
              "source_reference_vs_default": baseline,
              "versions": {"torch": torch.__version__, "tensorflow": str(references["tensorflow_version"])},
              "scope": "Host Q/DQ fidelity against source TFLite, source RGB image and synthetic inputs; no dataset mAP, no board execution"}
    print(json.dumps(report, indent=2), flush=True)
    # Acceptance is against the default optimized interpreter used for the
    # reference fixtures. The separately reported BUILTIN_REF is diagnostic:
    # its fixed-point requantization differs from default even before import.
    # 113 quantized operations accumulate rounding. Raw logits are bounded by
    # 8 LSB and mean .2 LSB, with independent real-image face-box requirements.
    report["acceptance"] = {"default_max_error_lsb": 8, "default_mean_error_lsb": .2,
                            "sample_min_iou": .9, "sample_same_count": True}
    for head in metrics["default"]["heads"]:
        if head["max_error_lsb"] > 8 or head["mean_error_lsb"] > .2:
            raise AssertionError(f"Excessive default head discrepancy: {head}")
    for sample in metrics["default"]["sample_detections"]:
        if not sample["same_count"] or any(value < .9 for value in sample["matched_ious"]):
            raise AssertionError(f"Decoded sample faces changed: {sample}")
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference-python", type=Path, required=True)
    parser.add_argument("--solution", type=Path, default=DEFAULT_SOLUTION)
    args = parser.parse_args(argv)
    torch.set_num_threads(4)
    _, solution, project, _, network = selected_project(args.solution, "object_detection")
    npu = solution["mlops"]["npu"]
    program = load_quantized_program(f"{npu['type'].lower()}-{npu['macs']}", project.parent, network)
    report = validate(program.module(), args.reference_python)
    eager = YoloFastest().eval()
    _, inputs = patterns()
    with torch.no_grad():
        for x in inputs:
            expected = eager(torch.from_numpy(x))
            actual = program.module()(torch.from_numpy(x))
            for lhs, rhs in zip(actual, expected):
                torch.testing.assert_close(lhs, rhs, rtol=0, atol=0)
    report["restored_cache_matches_eager_exactly"] = True
    report["artifacts"] = {key: sha256(project.parent / network[key]) for key in ("model", "quantize", "graph", "safetensors")}
    report["validation_sources"] = {name: sha256(Path(__file__).with_name(name))
                                    for name in ("validate.py", "tflite_reference.py", "patterns.py", "decode.py")}
    Path(__file__).with_name("validation.json").write_text(json.dumps(report, indent=2)+"\n", encoding="utf-8")


if __name__ == "__main__":
    main()
