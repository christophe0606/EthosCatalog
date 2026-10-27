#!/usr/bin/env python3
# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Compare the complete PyTorch reconstruction with unmodified upstream C."""
import ctypes
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import numpy as np
import torch
from safetensors.torch import save_file
from model import HERE, UPSTREAM, Blob, Prosody, Acoustic, build_inputs, sample


def metrics(reference, actual):
    error = np.asarray(actual) - np.asarray(reference)
    return {"max_absolute_error": float(np.abs(error).max()),
            "mean_absolute_error": float(np.abs(error).mean()),
            "rmse": float(np.sqrt(np.mean(error ** 2))),
            "reference_rms": float(np.sqrt(np.mean(np.asarray(reference) ** 2)))}


def reference():
    compiler = shutil.which("clang")
    if not compiler:
        raise RuntimeError("A host clang compiler is required for the independent original-C reference")
    build = HERE.parents[2] / "tmp/rvtts-reference-lpc"
    build.mkdir(parents=True, exist_ok=True)
    dll_path = build / "reference.dll"
    subprocess.run([compiler, "-shared", "-O2", "-D_CRT_SECURE_NO_WARNINGS", "-o", str(dll_path), str(HERE / "reference.c"),
                    str(UPSTREAM / "prosody_model_int8.c"), str(UPSTREAM / "acoustic_stage2.c")], check=True, cwd=build)
    dll = ctypes.CDLL(str(dll_path))
    dll.rv_init.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
    dll.rv_reference.argtypes = [ctypes.c_void_p] * 3 + [ctypes.c_uint] + [ctypes.c_void_p] * 2
    assert dll.rv_init(str(UPSTREAM / "PROSODY.bin").encode(), str(UPSTREAM / "ACOUSTIC.bin").encode()) == 0
    return dll


def main():
    torch.set_num_threads(1)
    models = {"prosody": Prosody().eval(), "acoustic": Acoustic().eval()}
    dll = reference()
    results, fixtures = {}, {}
    for i, frames in enumerate((1, 2, 7, 32, 96, 173)):
        phones, stress, words = sample(100 + i, frames)
        cond = np.empty((frames, 3), np.float32)
        features = np.empty((frames, 20), np.float32)
        ptr = lambda array: array.ctypes.data
        assert dll.rv_reference(ptr(phones), ptr(stress), ptr(words), frames, ptr(cond), ptr(features)) == 0
        px = build_inputs(phones, stress, words)
        with torch.no_grad():
            p, lv = models["prosody"](px)
            ax = build_inputs(phones, stress, conditioning=cond)
            a = models["acoustic"](ax)
        result = {"prosody": metrics(cond, p.numpy()[0]), "acoustic": metrics(features[:, 1:18], a.numpy()[0])}
        results[str(frames)] = result
        fixtures[f"case_{i}_phones"] = phones
        fixtures[f"case_{i}_stress"] = stress
        fixtures[f"case_{i}_words"] = words
        fixtures[f"case_{i}_prosody_input"] = px.numpy()
        fixtures[f"case_{i}_prosody_reference"] = cond
        fixtures[f"case_{i}_acoustic_input"] = ax.numpy()
        fixtures[f"case_{i}_acoustic_reference"] = features
        print(frames, result, flush=True)
        assert result["prosody"]["max_absolute_error"] < 1e-4
        assert result["acoustic"]["max_absolute_error"] < 2e-4
    np.savez(HERE / "source_fixtures.npz", **fixtures)
    tensors = {name + "." + key: value.contiguous() for name, model in models.items()
               for key, value in model.state_dict().items()}
    for name, b in (("prosody", Blob("prosody_weights_int8_meta.h", "PW8", "PROSODY.bin")),
                    ("acoustic", Blob("acoustic_stage2_weights_int8_meta.h", "AS2Q", "ACOUSTIC.bin"))):
        tensors[name + ".frontend.phone_embedding"] = b.tensor("PHONE_EMB", (41, 48))
        tensors[name + ".frontend.stress_embedding"] = b.tensor("STRESS_EMB", (4, 8))
        if name == "prosody":
            tensors[name + ".frontend.word_embedding"] = b.tensor("WORD_EMB", (14682, 8))
    save_file(tensors, HERE / "rvtts_float.safetensors",
              metadata={"upstream_commit": "b30af35f5366197f1a8845ef55af01a6c7c39a64", "upstream_license": "Apache-2.0"})
    dynamic_results = {}
    for name, model in models.items():
        x = torch.zeros(1, 8, 184 if name == "prosody" else 169)
        try:
            ep = torch.export.export(model, (x,), dynamic_shapes=({1: torch.export.Dim("frames", min=1, max=2048)},))
            torch.export.save(ep, HERE / (name + "_dynamic.pt2"))
            dynamic_results[name] = {"export": "PASS"}
        except Exception as error:
            dynamic_results[name] = {"export": "FAIL", "error": str(error)}
            torch.export.save(torch.export.export(model, (x,)), HERE / (name + "_static.pt2"))
    manifest = {"upstream_commit": "b30af35f5366197f1a8845ef55af01a6c7c39a64",
                "files": {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(UPSTREAM.iterdir()) if p.is_file()}}
    (HERE / "upstream_manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    (HERE / "float_equivalence.json").write_text(json.dumps({"reference": "unmodified upstream C, deterministic temperature=0",
                 "original_fp32_training_model_error": "not supplied by upstream", "cases": results, "dynamic_export": dynamic_results}, indent=2) + "\n")


if __name__ == "__main__":
    main()
