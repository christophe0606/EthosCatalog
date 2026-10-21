#!/usr/bin/env python3
# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Export complete UART-TTS neural methods with one shared flow backbone.

The tokenizer/text normalization stay in the application. All voice/text
prefill and autoregressive neural operations have exported methods; no flow
transformer weights are duplicated in a second prompt-only Vela blob.
"""
from __future__ import annotations
import argparse
from collections import Counter
from dataclasses import replace
import json
from pathlib import Path
import sys
import tempfile
import time
import traceback
import contextlib

import torch
import numpy as np
from safetensors.torch import load_file, save_file
from torchao.quantization.pt2e.observer import MinMaxObserver
from torchao.quantization.pt2e.quantize_pt2e import prepare_pt2e, convert_pt2e
from executorch.backends.arm.quantizer import EthosUQuantizer, get_symmetric_a16w8_quantization_config

LOCAL = Path(__file__).resolve().parent
ROOT = LOCAL.parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from quantized_model import compile_spec, save_quantized_program
from create_ai_layer import export_model
from model import load_model
from stages import TextEmbedding, Synthesize
from quantize import metrics
from import_q8 import sha256


def configuration(per_channel=True):
    config = get_symmetric_a16w8_quantization_config(is_per_channel=per_channel, epsilon=2**-24)
    activation = replace(config.input_activation, observer_or_fake_quant_ctr=MinMaxObserver.with_args(eps=2**-24))
    return replace(config, input_activation=activation, output_activation=activation)


def prepare(module, example, spec):
    graph = torch.export.export(module, example).module(check_guards=False)
    quantizer = EthosUQuantizer(spec)
    quantizer.set_global(None)
    for operator in (torch.ops.aten.linear.default, torch.ops.aten.conv1d.default,
                     torch.ops.aten.conv2d.default, torch.ops.aten.convolution.default):
        quantizer.set_node_target(operator, configuration())
    # The grouped depthwise Mimi upsample cannot use per-channel transpose
    # convolution weights in this backend. All transpose weights remain int8.
    for operator in (torch.ops.aten.conv_transpose1d.default, torch.ops.aten.conv_transpose2d.input):
        quantizer.set_node_target(operator, configuration(False))
    return prepare_pt2e(graph, quantizer)


def prompt(method, embeddings, cache, position, captured=None):
    for i in range(embeddings.shape[1]):
        if position >= cache.shape[-2]:
            raise ValueError("Flow state capacity exceeded by conditioning")
        sample = (embeddings[:, i:i + 1], cache, torch.tensor(float(position)))
        output = method(*sample)
        if captured is not None and (i == 0 or i == embeddings.shape[1] - 1):
            captured.append((sample, output))
        cache = output[1]
        position += 1
    return cache, position


def corpus(backbone, synthesize, embedding, source, voice, texts, seed, max_frames,
           collect=True):
    import sentencepiece as spm
    tokenizer = spm.SentencePieceProcessor(model_file=str(LOCAL / "weights/tokenizer.model"))
    backbone_samples, synth_samples, utterances = [], [], []
    for text in texts:
        fc, mc, states = source.initial_state()
        fc, position = prompt(backbone, voice, fc, 0, backbone_samples if collect else None)
        tokens = tokenizer.encode(text)
        for token in tokens:
            text_embedding = embedding(torch.tensor([[token]], dtype=torch.int32))
            fc, position = prompt(backbone, text_embedding, fc, position,
                                  backbone_samples if collect else None)
        current_embedding = source.source.linear(source.source.w("flow_lm.bos_emb").reshape(1, 1, 32), "flow_lm.input_linear") + source.conditions
        generator = torch.Generator().manual_seed(seed)
        eos_frame, audio, eos_values = None, [], []
        begin = time.monotonic()
        for i in range(max_frames):
            if position >= source.flow_capacity:
                raise ValueError("Flow state capacity exceeded before EOS")
            backbone_input = (current_embedding, fc, torch.tensor(float(position)))
            backbone_result = backbone(*backbone_input)
            b, fc = backbone_result
            noise = torch.randn(1, 32, generator=generator) * (0.8 ** 0.5)
            synth_input = (b, noise, mc, torch.tensor(float(16 * i)), states)
            synth_result = synthesize(*synth_input)
            latent, eos, pcm, current_embedding, mc, states = synth_result
            if collect:
                # Keep sparse complete cache samples, and every small decoder
                # sample. Observers see every prompt and synthesized frame.
                if i % 8 == 0:
                    backbone_samples.append((backbone_input, backbone_result))
                synth_samples.append((synth_input, synth_result))
            position += 1
            eos_values.append(eos.item()); audio.append(pcm.reshape(-1).numpy().copy())
            if eos.item() > 4 and eos_frame is None:
                eos_frame = i
            if eos_frame is not None and i >= eos_frame + (3 if len(text.split()) <= 4 else 1):
                break
        utterances.append(dict(text=text, token_ids=tokens, frames=len(audio), eos_frame=eos_frame,
                               completed_eos=eos_frame is not None, pcm=np.concatenate(audio),
                               eos_logits=eos_values, host_seconds=time.monotonic() - begin))
    return backbone_samples, synth_samples, utterances


def write_wavs(kind, utterances):
    import wave
    result = []
    for i, utterance in enumerate(utterances):
        row = {key: value for key, value in utterance.items() if key != "pcm"}
        pcm = utterance["pcm"]
        path = LOCAL / "weights" / f"staged_{kind}_{i}.wav"
        with wave.open(str(path), "wb") as f:
            f.setnchannels(1); f.setsampwidth(2); f.setframerate(24000)
            f.writeframes((np.clip(pcm, -1, 1) * 32767).astype("<i2").tobytes())
        row.update(file=path.relative_to(LOCAL).as_posix(), sha256=sha256(path),
                   peak=float(np.abs(pcm).max()), rms=float(np.sqrt(np.mean(pcm ** 2))),
                   clipped_samples=int(np.count_nonzero(np.abs(pcm) > 1)),
                   audio_seconds=pcm.size / 24000)
        result.append(row)
    return result


def inspect_pte(path):
    from executorch.exir._serialize._program import deserialize_pte_binary
    file = deserialize_pte_binary(path.read_bytes())
    program = file.program
    return dict(methods={p.name: dict(operators=[f"{o.name}.{o.overload}" for o in p.operators],
                                    planned_buffer_sizes=p.non_const_buffer_sizes,
                                    delegates=len(p.delegates)) for p in program.execution_plan},
                constant_buffer_bytes=sum(len(x.storage) for x in program.constant_buffer))


def main():
    import yaml
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--solution", type=Path, default=ROOT / "EthosCatalog.csolution.yml")
    parser.add_argument("--project", type=Path, default=LOCAL.parent,
                        help="Phonon project folder or .cproject.yml path")
    parser.add_argument("--lower", action="store_true")
    parser.add_argument("--max-frames", type=int, default=96)
    parser.add_argument("--flow-capacity", type=int, default=512,
                        help="Voice+text+generation token capacity; no context may be truncated")
    parser.add_argument("--memory-mode", choices=["Shared_Sram", "Dedicated_Sram"], default="Shared_Sram")
    args = parser.parse_args()
    project_dir = args.project.resolve()
    if project_dir.is_file():
        project_dir = project_dir.parent
    if project_dir != LOCAL.parent:
        parser.error("--project must identify this Phonon project")
    torch.set_num_threads(4)
    output = LOCAL / "artifacts" / "staged"; output.mkdir(parents=True, exist_ok=True)
    compile_temp = output / "tmp"; compile_temp.mkdir(exist_ok=True)
    tempfile.tempdir = str(compile_temp.resolve())
    solution = yaml.safe_load(args.solution.read_text())["solution"]
    network = yaml.safe_load((LOCAL / "network.yml").read_text())["network"]
    network["vela"]["memory_mode"] = args.memory_mode
    spec = compile_spec(solution, LOCAL.parent, network)
    spec.dump_intermediate_artifacts_to(str(output / args.memory_mode))
    report = dict(status="in_progress", quality_accepted=False, hardware_realtime_verified=False,
                  policy="linear+conv a16w8; float attention, masks, normalization and state; per-tensor transpose weights",
                  seed=20261021, temperature=0.8, source_revision="52433c46975bfa13144c56204c9f4ee1c5b148aa",
                  calibration="complete generated utterances including all voice/text conditioning, voiced/midstream audio and EOS",
                  memory_mode=args.memory_mode)
    report_path = LOCAL / "staged_metrics.json"
    start = time.monotonic()
    try:
        source = load_model(flow_capacity=args.flow_capacity)
        embedding = TextEmbedding(source).eval()
        synth = Synthesize(source).eval()
        fc, mc, states = source.initial_state()
        voice_latents = load_file(LOCAL / "weights/default-voice.safetensors")["speaker_wavs"].transpose(1, 2)
        voice = source.source.linear(voice_latents, "flow_lm.condition_provider.conditioners.speaker_wavs.output_proj")
        bos = source.source.linear(source.source.w("flow_lm.bos_emb").reshape(1, 1, 32), "flow_lm.input_linear") + source.conditions
        save_file(dict(default_voice_embeddings=voice.contiguous(), bos_embedding=bos.contiguous()),
                  LOCAL / "weights/startup.safetensors", metadata={"source_revision": report["source_revision"], "license": "Apache-2.0"})
        with torch.no_grad():
            b, _ = source.backbone(bos, fc, torch.tensor(0.0))
            synth_example = (b, torch.zeros(1, 32), mc, torch.tensor(0.0), states)
            # Exact stage chaining before quantization, including next embedding.
            wanted = source(source.source.w("flow_lm.bos_emb").reshape(1, 32), synth_example[1], fc,
                            torch.tensor(0.0), mc, torch.tensor(0.0), states)
            actual = synth(*synth_example)
            report["float_chain_error"] = [metrics(a, b) for a, b in zip(actual[:3], wanted[:3])]
            if any(row["max_absolute_error"] != 0 for row in report["float_chain_error"]):
                raise ValueError("Splitting the model changed eager frame outputs")
            print("[staged] preparing shared backbone and synthesize", flush=True)
            backbone = prepare(source.backbone, (bos, fc, torch.tensor(0.0)), spec)
            synthesize = prepare(synth, synth_example, spec)
            texts = ["Hello from Phonon.", "The quick brown fox jumps over the lazy dog."]
            print("[staged] calibrating complete voice/text prompts and utterances", flush=True)
            bs, ss, originals = corpus(backbone, synthesize, embedding, source, voice, texts,
                                        report["seed"], args.max_frames)
            report["source_utterances"] = write_wavs("q8", originals)
            print(f"[staged] calibrated {sum(u['frames'] for u in originals)} voiced/EOS frames", flush=True)
            qbackbone, qsynth = convert_pt2e(backbone, fold_quantize=True), convert_pt2e(synthesize, fold_quantize=True)
            report["fixed_state_backbone_errors"] = [metrics(qbackbone(*sample)[0], ref[0]) for sample, ref in bs]
            report["fixed_state_synthesis_errors"] = [{name: metrics(a, b) for name, a, b in
                zip(("latent", "eos", "pcm", "next_embedding"), qsynth(*sample)[:4], ref[:4])} for sample, ref in ss]
            _, _, generated = corpus(qbackbone, qsynth, embedding, source, voice, texts,
                                      report["seed"], args.max_frames, collect=False)
            report["ptq_free_recurrence_utterances"] = write_wavs("ptq", generated)
            report["source_vs_ptq_aligned_pcm"] = [metrics(torch.from_numpy(q["pcm"][:min(len(q["pcm"]), len(f["pcm"]))]),
                torch.from_numpy(f["pcm"][:min(len(q["pcm"]), len(f["pcm"]))])) for q, f in zip(generated, originals)]
        methods = {"backbone": torch.export.export(qbackbone, (bos, fc, torch.tensor(0.0))),
                   "synthesize": torch.export.export(qsynth, synth_example),
                   "text_embedding": torch.export.export(embedding, (torch.tensor([[1]], dtype=torch.int32),))}
        manifest = dict(format="ethos-catalog-methods-v1", target=spec.target, methods={},
                        cpu_methods=["text_embedding"], startup=dict(file="model/weights/startup.safetensors",
                        sha256=sha256(LOCAL / "weights/startup.safetensors")), quality_accepted=False,
                        flow_capacity=args.flow_capacity, default_voice_tokens=int(voice.shape[1]),
                        state_shapes=dict(flow_kv=list(fc.shape), mimi_kv=list(mc.shape),
                                          convolution=[list(value.shape) for value in states]),
                        source_files={f"model/{name}": sha256(LOCAL / name) for name in
                                      ("model.py", "stages.py", "export_stages.py", "import_q8.py", "startup_assets.py")})
        for name, program in methods.items():
            config = dict(network, quantize="model/export_stages.py", graph=f"model/{name}_quantized.pt2",
                          safetensors=f"model/{name}_quantized.safetensors")
            save_quantized_program(program, spec.target, LOCAL.parent, config)
            manifest["methods"][name] = {key: config[key] for key in ("graph", "safetensors")}
            manifest["methods"][name]["graph_sha256"] = sha256(LOCAL.parent / config["graph"])
            manifest["methods"][name]["safetensors_sha256"] = sha256(LOCAL.parent / config["safetensors"])
        from startup_assets import create_voice_prefix
        manifest["startup"].update(create_voice_prefix(methods["backbone"].module(check_guards=False), voice, bos, manifest))
        (LOCAL / "method_manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        report["status"] = "staged_quantized_unvalidated"
        report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print("[staged] saved complete shared-backbone method caches", flush=True)
        if args.lower:
            from lower_stages import inspect_edge
            def capture(edge):
                report["coverage"] = inspect_edge(edge)
            with (output / f"vela_{args.memory_mode}.log").open("w", encoding="utf-8") as log, contextlib.redirect_stdout(log):
                pte, delegates = export_model(methods, spec, cpu_methods=("text_embedding",), inspection_callback=capture)
            path = output / f"phonon_{args.memory_mode}.pte"; path.write_bytes(pte)
            report.update(status="staged_lowered_unvalidated", pte_bytes=len(pte), pte_sha256=sha256(path), delegates=delegates)
            report["serialized_program"] = inspect_pte(path)
            print(f"[staged] PTE {len(pte)} bytes, {delegates} Ethos delegates", flush=True)
    except Exception as error:
        report.update(status="failed", error=f"{type(error).__name__}: {error}", traceback=traceback.format_exc())
        raise
    finally:
        report["elapsed_seconds"] = time.monotonic() - start
        report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
