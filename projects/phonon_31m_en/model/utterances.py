#!/usr/bin/env python3
# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Generate reviewable whole-utterance WAVs; do not infer a perceptual score."""
import argparse
import json
from pathlib import Path
import time
import wave
import numpy as np
import torch
from safetensors.torch import load_file
from model import load_model
from import_q8 import sha256

ROOT = Path(__file__).resolve().parent


def synthesize(kind, text, output, seed, max_frames):
    import sentencepiece as spm
    import torchao.quantization.pt2e.quantize_pt2e  # Register Q/DQ for export load.
    if kind == "float":
        source = load_model(ROOT / "weights/float_reference.safetensors")
    else:
        source = load_model()
    model = (torch.export.load(ROOT / "model_quantized.pt2").module(check_guards=False)
             if kind == "ptq" else source)
    tokenizer = spm.SentencePieceProcessor(model_file=str(ROOT / "weights/tokenizer.model"))
    voice = load_file(ROOT / "weights/default-voice.safetensors")["speaker_wavs"].transpose(1, 2)
    voice = source.source.linear(voice, "flow_lm.condition_provider.conditioners.speaker_wavs.output_proj")
    fc, mc, states = source.initial_state()
    fc, position = source.prompt(voice, fc, 0)
    ids = tokenizer.encode(text)
    fc, position = source.prompt(source.text_embedding(torch.tensor([ids])), fc, position)
    previous = source.source.w("flow_lm.bos_emb").reshape(1, 32)
    generator = torch.Generator().manual_seed(seed)
    chunks, eos_values = [], []
    end_frame = None
    start = time.monotonic()
    for i in range(max_frames):
        if position >= source.flow_capacity:
            raise ValueError("Flow state capacity exhausted before EOS")
        noise = torch.randn(1, 32, generator=generator) * (0.8 ** 0.5)
        previous, eos, audio, fc, mc, states = model(previous, noise, fc, torch.tensor(float(position)),
                                                  mc, torch.tensor(float(16 * i)), states)
        position += 1
        chunks.append(audio.reshape(-1).numpy().copy())
        eos_values.append(eos.item())
        if eos.item() > 4 and end_frame is None:
            end_frame = i
        # Upstream retains three frames after EOS for <=4-word inputs.
        if end_frame is not None and i >= end_frame + (3 if len(text.split()) <= 4 else 1):
            break
    elapsed = time.monotonic() - start
    pcm = np.concatenate(chunks)
    if not np.isfinite(pcm).all():
        raise ValueError("Nonfinite waveform")
    clipped = int(np.count_nonzero(np.abs(pcm) > 1))
    output.parent.mkdir(exist_ok=True, parents=True)
    with wave.open(str(output), "wb") as wav:
        wav.setnchannels(1); wav.setsampwidth(2); wav.setframerate(24000)
        wav.writeframes((np.clip(pcm, -1, 1) * 32767).astype("<i2").tobytes())
    result = dict(kind=kind, text=text, seed=seed, temperature=0.8, sample_rate=24000,
                  wav_file=output.relative_to(ROOT).as_posix(), wav_sha256=sha256(output),
                  frames=len(chunks), audio_seconds=pcm.size / 24000, eos_frame=end_frame,
                  completed_eos=end_frame is not None, eos_logits=eos_values,
                  peak=float(np.abs(pcm).max()), rms=float(np.sqrt(np.mean(pcm ** 2))),
                  samples_clipped=clipped, host_synthesis_seconds=elapsed,
                  hardware_realtime_measurement=False,
                  prompt_method="q8 eager voice/text prompt, then PTQ free recurrence" if kind == "ptq" else "same model for prompting and free recurrence",
                  perceptual_quality_verified=False)
    if kind == "ptq":
        result["ptq_graph_sha256"] = sha256(ROOT / "model_quantized.pt2")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--text", default="Hello from Phonon.")
    parser.add_argument("--seed", type=int, default=20261021)
    parser.add_argument("--max-frames", type=int, default=128)
    parser.add_argument("--kinds", nargs="+", default=["q8", "float", "ptq"], choices=["q8", "float", "ptq"])
    args = parser.parse_args()
    torch.set_num_threads(4)
    results = []
    with torch.no_grad():
        for kind in args.kinds:
            result = synthesize(kind, args.text, ROOT / "weights" / f"utterance_{kind}.wav", args.seed, args.max_frames)
            results.append(result)
            print(f"{kind}: {result['frames']}frames EOS={result['eos_frame']} peak={result['peak']:.6f} rms={result['rms']:.6f}", flush=True)
    (ROOT / "utterance_metrics.json").write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
