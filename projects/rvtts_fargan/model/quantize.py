#!/usr/bin/env python3
# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Calibrate the complete FARGAN init/frame graphs and copy shared model caches."""
import argparse
from dataclasses import replace
import hashlib
import json
from pathlib import Path
import shutil
import sys
import numpy as np
import torch
import yaml
from model import HERE, STATE, Fargan, FarganInit, load_arrays, frame_input, pitch_indices
from validate import metrics
ROOT = HERE.parents[2]
SHARED = HERE.parents[1] / "rvtts_lpc/model"
sys.path.insert(0, str(ROOT / "scripts"))
from project_config import DEFAULT_SOLUTION, selected_project
from quantized_model import compile_spec, save_quantized_program, method_configs


def corrected(features):
    result = features.copy()
    pitch = result[:, 18]
    result[:, 18] = np.clip((pitch-pitch.mean())/max(float(pitch.std()),1e-5),-2.5,2.5)*.3615+.2120
    return result


def trace(models, features, capture=None):
    inputs, last = frame_input(features[0])
    indices = pitch_indices(last)
    zero_state = torch.zeros(1, STATE)
    if capture is not None:
        capture["fargan_init"].append((inputs.clone(), indices.clone(), zero_state.clone()))
    state = models["fargan_init"](inputs, indices, zero_state)
    samples = []
    for features_ in features:
        x, period = frame_input(features_)
        args = (torch.cat((x, state), 1), pitch_indices(last))
        if capture is not None:
            capture["fargan_frame"].append(tuple(v.clone() for v in args))
        out = models["fargan_frame"](*args)
        samples.append(out[:, :160])
        state = out[:, 160:]
        last = period
    excitation = torch.cat(samples, 1).numpy().reshape(-1)
    pcm = excitation.copy()
    memory = np.float32(0)
    for i in range(len(pcm)):
        pcm[i] += np.float32(.85)*memory
        memory = pcm[i]
    return excitation, pcm


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--solution", type=Path, default=DEFAULT_SOLUTION)
    parser.add_argument("--project", default="rvtts_fargan")
    args = parser.parse_args()
    _, solution, project, config, network = selected_project(args.solution, args.project)
    if project.parent != HERE.parent:
        parser.error("This quantizer owns rvtts_fargan")
    torch.set_num_threads(1)
    for name in ("prosody_pre", "prosody_gru", "prosody_head", "acoustic"):
        for extension in ("pt2", "safetensors"):
            source = SHARED / f"{name}_quantized.{extension}"
            shutil.copyfile(source, HERE / source.name)
    for name in ("PROSODY.bin", "ACOUSTIC.bin"):
        shutil.copyfile(SHARED / "upstream" / name, HERE / "upstream" / name)
    data = np.load(SHARED / "source_fixtures.npz")
    models = {"fargan_init": FarganInit().eval(), "fargan_frame": Fargan().eval()}
    calibration = {name: [] for name in models}
    with torch.no_grad():
        for i in range(1, 5):
            trace(models, corrected(data[f"case_{i}_acoustic_reference"]), calibration)
    heldout = {name: [] for name in models}
    features = corrected(data["case_5_acoustic_reference"])
    with torch.no_grad():
        reference_exc, reference_pcm = trace(models, features, heldout)
    from executorch.backends.arm.quantizer import EthosUQuantizer, get_symmetric_a16w8_quantization_config
    from torchao.quantization.pt2e.quantize_pt2e import prepare_pt2e, convert_pt2e
    from torchao.quantization.pt2e.observer import MinMaxObserver
    spec = compile_spec(solution, project.parent, network)
    quantized, method_metrics, fixtures = {}, {}, {}
    header = [(SHARED / "model_test_data.h").read_text(), "#include <stdint.h>",
              "#define RVTTS_FARGAN_STATE 964", "#define RVTTS_FARGAN_INPUT 996", "#define RVTTS_FARGAN_OUTPUT 1124"]
    for name, model in models.items():
        example = calibration[name][0]
        gm = torch.export.export(model, example).module(check_guards=False)
        qconfig = get_symmetric_a16w8_quantization_config(epsilon=2**-24)
        activation = replace(qconfig.input_activation, observer_or_fake_quant_ctr=MinMaxObserver.with_args(eps=2**-24))
        qconfig = replace(qconfig, input_activation=activation, output_activation=activation)
        prepared = prepare_pt2e(gm, EthosUQuantizer(spec).set_global(qconfig))
        with torch.no_grad():
            for values in calibration[name]:
                prepared(*values)
            for observer in prepared.modules():
                if isinstance(observer, MinMaxObserver) and observer.dtype == torch.int16:
                    observer.min_val.mul_(1.5); observer.max_val.mul_(1.5)
        converted = convert_pt2e(prepared, fold_quantize=True)
        program = torch.export.export(converted, example)
        save_quantized_program(program, spec.target, project.parent, method_configs(network)[name])
        quantized[name] = converted
        with torch.no_grad():
            references = np.concatenate([model(*values).numpy().reshape(-1) for values in heldout[name]])
            outputs = np.concatenate([converted(*values).numpy().reshape(-1) for values in heldout[name]])
        method_metrics[name] = {"calibration_calls": len(calibration[name]), "evaluation_calls": len(heldout[name]),
                                **metrics(references, outputs)}
        values = heldout[name][min(10, len(heldout[name])-1)]
        with torch.no_grad(): expected = converted(*values).numpy()
        fixtures[name+"_input"] = values[0].numpy()
        fixtures[name+"_indices"] = values[1].numpy()
        if len(values) == 3:
            fixtures[name+"_zero_state"] = values[2].numpy()
        fixtures[name+"_expected"] = expected
        scales = [float(n.args[1]) for n in program.graph.nodes if n.target == torch.ops.quantized_decomposed.quantize_per_tensor.default]
        scale = scales[-1]
        method_metrics[name]["output_quantization_scale"] = scale
        header.append(f"#define RVTTS_{name.upper()}_ATOL {max(4*scale,1e-4):.9e}f")
        arrays = [("input", values[0].numpy(), "float"), ("indices", values[1].numpy(), "int64_t")]
        if len(values) == 3:
            arrays.append(("zero_state", values[2].numpy(), "float"))
        arrays.append(("expected", expected, "float"))
        for suffix, array, ctype in arrays:
            numbers = ",".join(f"{float(v):.9e}f" if ctype == "float" else str(int(v)) for v in array.reshape(-1))
            header.append(f"static const {ctype} rvtts_{name}_{suffix}[{array.size}] = {{{numbers}}};")
        print(name, method_metrics[name], flush=True)
    with torch.no_grad():
        excitation, pcm = trace(quantized, features)
    pcm_error = metrics(reference_pcm, pcm)
    pcm_error["snr_db"] = float(20*np.log10(max(pcm_error["reference_rms"],1e-20)/max(pcm_error["rmse"],1e-20)))
    report = {"shared": json.loads((SHARED / "conversion_metrics.json").read_text()),
              "fargan": {"scheme": "symmetric int16 activations/per-channel int8 weights, post-training, no training",
                         "calibration": "4 synthetic source feature sequences, 2/7/32/96 frames; MinMax with 50% activation range guard after diagnosing held-out reciprocal-gain clipping",
                         "evaluation": "173-frame held-out source feature sequence; exact source pitch normalization",
                         "methods": method_metrics, "closed_loop_pcm": pcm_error,
                         "limitation": "Waveform error and SNR do not certify perceptual speech quality; listening validation remains required."}}
    (HERE / "conversion_metrics.json").write_text(json.dumps(report, indent=2)+"\n")
    (HERE / "model_test_data.h").write_text("\n".join(header)+"\n")
    np.savez(HERE / "quantized_fixtures.npz", **fixtures, float_pcm=reference_pcm, quantized_pcm=pcm)
    embedding = load_arrays()["cond_net_pembed_weights_float"]
    params = ["// Copyright (c) 2023 Amazon. BSD-3-Clause; see upstream/OPUS-COPYING.", "// Generated from the official Opus/rvTTS pitch embedding.", "#pragma once",
              "static const float rvtts_fargan_pitch_embedding[2688] = {"+",".join(f"{float(v):.9e}f" for v in embedding)+"};"]
    (HERE / "fargan_params.h").write_text("\n".join(params)+"\n")
    manifest = {"rvtts_commit": "b30af35f5366197f1a8845ef55af01a6c7c39a64", "files": {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted((HERE/"upstream").iterdir()) if p.is_file()},
                "shared_model_sha256": hashlib.sha256((SHARED/"model.py").read_bytes()).hexdigest()}
    (HERE / "upstream_manifest.json").write_text(json.dumps(manifest, indent=2)+"\n")
    content = yaml.safe_load(config.read_text()); content["network"]["quantization"]["error"] = report
    config.write_text(yaml.safe_dump(content, sort_keys=False))
    print("closed_loop_pcm", pcm_error, flush=True)


if __name__ == "__main__":
    main()
