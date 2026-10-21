#!/usr/bin/env python3
# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Calibrate the complete trained q8 Phonon frame and attempt Ethos-U lowering.

Direct execution writes only project-local artifacts, never root ai_layer.
Synthetic calibration is for compiler feasibility; it is not a speech-quality
validation or permission to publish an unvalidated firmware model.
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import sys
import traceback
import time
import faulthandler
from dataclasses import replace
import tempfile

ROOT = Path(__file__).resolve().parents[3]
LOCAL = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / "scripts"))
from quantized_model import compile_spec, save_quantized_program
from model import load_model


def inputs(model, texts, frames):
    """Small generated text/latent corpus, no external calibration dataset."""
    import torch
    import sentencepiece as spm
    from safetensors.torch import load_file
    tokenizer = spm.SentencePieceProcessor(model_file=str(LOCAL / "weights/tokenizer.model"))
    voice = load_file(LOCAL / "weights/default-voice.safetensors")["speaker_wavs"].transpose(1, 2)
    prefix = "flow_lm.condition_provider.conditioners.speaker_wavs.output_proj"
    voice = model.source.linear(voice, prefix)
    generator = torch.Generator().manual_seed(20261021)
    for text in texts:
        fc, mc, states = model.initial_state()
        fc, position = model.prompt(voice, fc, 0)
        tokens = torch.tensor([tokenizer.encode(text)], dtype=torch.int64)
        fc, position = model.prompt(model.text_embedding(tokens), fc, position)
        previous = model.source.w("flow_lm.bos_emb").reshape(1, 32)
        for i in range(frames):
            if position >= model.flow_capacity:
                raise ValueError("Calibration exceeded the exact Flow attention context")
            noise = torch.randn(1, 32, generator=generator) * (0.8 ** 0.5)
            sample = (previous, noise, fc, torch.tensor(float(position)),
                      mc, torch.tensor(float(i * 16)), states)
            yield sample
            previous, _, _, fc, mc, states = model(*sample)
            position += 1


def metrics(actual, reference):
    import torch
    difference = (actual.float() - reference.float()).flatten()
    denominator = reference.float().flatten().square().mean().sqrt().item()
    rmse = difference.square().mean().sqrt().item()
    return dict(max_absolute_error=difference.abs().max().item(), rmse=rmse,
                normalized_rmse=rmse / denominator if denominator else None,
                finite=bool(torch.isfinite(actual).all()))


def main():
    import torch
    import yaml
    from executorch.backends.arm.quantizer import EthosUQuantizer, get_symmetric_quantization_config, get_symmetric_a16w8_quantization_config
    from torchao.quantization.pt2e.quantize_pt2e import convert_pt2e, prepare_pt2e
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--solution", type=Path, default=ROOT / "EthosCatalog.csolution.yml")
    parser.add_argument("--project", default="phonon_31m_en")
    parser.add_argument("--frames", type=int, default=8)
    parser.add_argument("--lower", action="store_true")
    parser.add_argument("--policy", choices=["linear_a16w8", "global_int8"], default="linear_a16w8")
    parser.add_argument("--memory-mode", choices=["Shared_Sram", "Dedicated_Sram"], default="Shared_Sram")
    args = parser.parse_args()
    torch.set_num_threads(4)
    artifacts = LOCAL / "artifacts"; artifacts.mkdir(exist_ok=True)
    compile_temp = artifacts / "tmp"; compile_temp.mkdir(exist_ok=True)
    tempfile.tempdir = str(compile_temp.resolve())
    faulthandler.enable()
    faulthandler.dump_traceback_later(60, repeat=True)
    solution = yaml.safe_load(args.solution.read_text())["solution"]
    network = yaml.safe_load((LOCAL / "network.yml").read_text())["network"]
    network["vela"]["memory_mode"] = args.memory_mode
    spec = compile_spec(solution, LOCAL.parent, network)
    report = dict(status="running", target=spec.target, memory_mode=args.memory_mode,
                  calibration="generated normalized English text, default voice and recurrent q8 inference states",
                  random_seed=20261021, source_quantization="GGML Q8_0 + BF16; not Ethos quantization",
                  policy=args.policy,
                  ethos_quantization="selective linear/conv int16 activations, int8 weights; float attention masking/norm/state; MinMax calibration" if args.policy == "linear_a16w8" else "affine per-tensor int8 activations; symmetric per-channel int8 weights; MinMax calibration",
                  upstream_q8_error="Not published in the pinned model card; no original-float quality claim",
                  speech_quality_verified=False, hardware_realtime_verified=False)
    start = time.monotonic()
    try:
        model = load_model()
        texts = ["Hello from Phonon.", "The quick brown fox jumps over the lazy dog."]
        with torch.no_grad():
            samples = list(inputs(model, texts, args.frames))
            reference = [model(*sample) for sample in samples]
        report["calibration_texts"] = texts; report["calibration_frames"] = len(samples)
        print(f"[phonon] {len(samples)} generated calibration frames; exporting full frame", flush=True)
        example = samples[0]
        program = torch.export.export(model, example)
        torch.export.save(program, artifacts / "phonon_float.pt2")
        exported = program.module(check_guards=False)
        with torch.no_grad():
            report["export_error"] = [metrics(a, b) for a, b in zip(exported(*example)[:3], reference[0][:3])]
        quantizer = EthosUQuantizer(spec)
        config = (get_symmetric_a16w8_quantization_config() if args.policy == "linear_a16w8" else
                  get_symmetric_quantization_config(is_per_channel=True))
        from torchao.quantization.pt2e.observer import MinMaxObserver
        activation = replace(config.input_activation, observer_or_fake_quant_ctr=MinMaxObserver.with_args(eps=2**-16))
        config = replace(config, input_activation=activation, output_activation=activation)
        if args.policy == "linear_a16w8":
            quantizer.set_global(None)
            for operator in (torch.ops.aten.linear.default, torch.ops.aten.conv1d.default,
                             torch.ops.aten.conv2d.default, torch.ops.aten.conv_transpose1d.default,
                             torch.ops.aten.conv_transpose2d.input, torch.ops.aten.convolution.default):
                quantizer.set_node_target(operator, config)
        else:
            quantizer.set_global(config)
        print("[phonon] preparing PTQ", flush=True)
        prepared = prepare_pt2e(exported, quantizer)
        with torch.no_grad():
            for i, sample in enumerate(samples):
                prepared(*sample)
                print(f"[phonon] calibrated {i + 1}/{len(samples)}", flush=True)
        print("[phonon] converting calibrated observers to int8", flush=True)
        quantized = convert_pt2e(prepared, fold_quantize=True)
        print("[phonon] measuring PTQ error", flush=True)
        with torch.no_grad():
            errors = [tuple(metrics(a, b) for a, b in zip(quantized(*sample)[:3], wanted[:3]))
                      for sample, wanted in zip(samples, reference)]
        report["ptq_errors"] = errors
        report["max_latent_nrmse"] = max(frame[0]["normalized_rmse"] for frame in errors)
        report["max_pcm_nrmse"] = max(frame[2]["normalized_rmse"] for frame in errors)
        print(f"[phonon] PTQ max latent NRMSE={report['max_latent_nrmse']:.6f}, PCM NRMSE={report['max_pcm_nrmse']:.6f}", flush=True)
        if not all(item["finite"] for frame in errors for item in frame):
            raise ValueError("PTQ generated nonfinite outputs")
        program = torch.export.export(quantized, example)
        save_quantized_program(program, spec.target, LOCAL.parent, network)
        report["status"] = "quantized_pre_vela"
        report["quantized_nodes"] = len(list(program.graph.nodes))
        (LOCAL / f"conversion_metrics_{args.policy}.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print("[phonon] saved complete pre-Vela int8 graph and safetensors", flush=True)
        if args.lower:
            import executorch.kernels.quantized  # noqa: F401
            from executorch.backends.arm.ethosu import EthosUPartitioner
            from executorch.exir import EdgeCompileConfig, ExecutorchBackendConfig, to_edge_transform_and_lower
            spec.dump_intermediate_artifacts_to(str(artifacts / args.memory_mode))
            print("[phonon] attempting complete ExecuTorch/Vela lowering", flush=True)
            edge = to_edge_transform_and_lower(program, partitioner=[EthosUPartitioner(spec)],
                                              compile_config=EdgeCompileConfig(_check_ir_validity=False))
            delegates = sum(n.op == "call_function" and "executorch_call_delegate" in str(n.target)
                            for n in edge.exported_program().graph.nodes)
            report["delegates"] = delegates
            output = edge.to_executorch(ExecutorchBackendConfig(extract_delegate_segments=False))
            buffer = bytes(output.buffer)
            (artifacts / f"phonon_{args.memory_mode}.pte").write_bytes(buffer)
            report["pte_bytes"] = len(buffer)
            report["status"] = "lowered_unvalidated" if delegates else "no_ethos_delegate"
    except Exception as error:
        report["status"] = "failed"
        report["error"] = f"{type(error).__name__}: {error}"
        report["traceback"] = traceback.format_exc()
        raise
    finally:
        faulthandler.cancel_dump_traceback_later()
        report["elapsed_seconds"] = time.monotonic() - start
        (LOCAL / "conversion_metrics.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        (LOCAL / f"conversion_metrics_{args.policy}.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
