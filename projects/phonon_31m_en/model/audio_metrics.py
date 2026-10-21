#!/usr/bin/env python3
# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Phase-tolerant diagnostics for deterministic q8/PTQ utterance fixtures.

These are signal metrics, not intelligibility, speaker similarity, or a
perceptual quality acceptance test. No ASR model or external dataset is used.
"""
from pathlib import Path
import argparse
import json
import wave
import numpy as np

LOCAL = Path(__file__).resolve().parent
RATE = 24000


def read(path):
    with wave.open(str(path), "rb") as wav:
        assert wav.getframerate() == RATE and wav.getnchannels() == 1
        return np.frombuffer(wav.readframes(wav.getnframes()), dtype="<i2").astype(np.float64) / 32767


def mel_power(audio):
    frames = np.lib.stride_tricks.sliding_window_view(np.pad(audio, (300, 300)), 600)[::240]
    power = np.abs(np.fft.rfft(frames * np.hanning(600), 1024)) ** 2
    frequencies = np.fft.rfftfreq(1024, 1 / RATE)
    mel = lambda hz: 2595 * np.log10(1 + hz / 700)
    hz = lambda m: 700 * (10 ** (m / 2595) - 1)
    edges = hz(np.linspace(mel(0), mel(RATE / 2), 82))
    filters = np.stack([np.maximum(0, np.minimum((frequencies - left) / (middle - left),
                         (right - frequencies) / (right - middle)))
                         for left, middle, right in zip(edges, edges[1:], edges[2:])])
    return power @ filters.T


def pitch(audio):
    frames = np.lib.stride_tricks.sliding_window_view(audio, 960)[::480]
    values = []
    for frame in frames:
        frame = frame - frame.mean()
        if np.sqrt(np.mean(frame ** 2)) < 0.005:
            continue
        corr = np.correlate(frame * np.hanning(960), frame * np.hanning(960), "full")[959:]
        lag = int(np.argmax(corr[40:401])) + 40
        if corr[lag] / max(corr[0], 1e-12) >= 0.6:
            values.append(RATE / lag)
    return dict(voiced_frames=len(values), total_frames=len(frames),
                median_hz=float(np.median(values)) if values else None)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", type=Path, default=LOCAL / "staged_metrics.json")
    parser.add_argument("--output", type=Path, default=LOCAL / "audio_metrics.json")
    args = parser.parse_args()
    staged = json.loads(args.report.read_text())
    result = dict(quality_accepted=False,
                  limitations="Signal-only diagnostics on a small English corpus and one default voice; no intelligibility, perceptual score, ASR or speaker identity validation.",
                  settings=dict(sample_rate=RATE, mel_bands=80, fft_size=1024, window_samples=600,
                                hop_samples=240, active_threshold_db=-50, pitch_window_samples=960,
                                pitch_hop_samples=480, pitch_range_hz=[60, 600], pitch_correlation_threshold=0.6), utterances=[])
    for source, ptq in zip(staged["source_utterances"], staged["ptq_free_recurrence_utterances"]):
        a, b = read(LOCAL / source["file"]), read(LOCAL / ptq["file"])
        count = min(len(a), len(b)); a, b = a[:count], b[:count]
        ma, mb = mel_power(a), mel_power(b)
        reference = max(ma.max(), 1e-12)
        active = ma.sum(1) > ma.sum(1).max() * 1e-5
        da, db = 10 * np.log10(np.maximum(ma, reference * 1e-8)), 10 * np.log10(np.maximum(mb, reference * 1e-8))
        ea, eb = ma.sum(1), mb.sum(1)
        av, bv = ma.mean(0), mb.mean(0)
        result["utterances"].append(dict(text=source["text"], source_sha256=source["sha256"], ptq_sha256=ptq["sha256"],
            source_audio_seconds=source["audio_seconds"], ptq_audio_seconds=ptq["audio_seconds"],
            eos_frame_difference=ptq["eos_frame"] - source["eos_frame"] if ptq["completed_eos"] else None,
            aligned_active_log_mel_mean_absolute_db=float(np.abs(da[active] - db[active]).mean()),
            mel_spectral_convergence=float(np.linalg.norm(mb - ma) / np.linalg.norm(ma)),
            mean_mel_spectrum_cosine=float(av @ bv / (np.linalg.norm(av) * np.linalg.norm(bv))),
            energy_envelope_correlation=float(np.corrcoef(ea, eb)[0, 1]),
            source_pitch=pitch(a), ptq_pitch=pitch(b)))
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
