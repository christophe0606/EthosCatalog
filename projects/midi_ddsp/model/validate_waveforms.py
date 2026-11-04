# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Describe host waveform differences without assigning a perceptual pass.

Consumes provenance-checked validate_closed_loop.py checkpoints. Independent
categorical paths can drift in phase, so both original pitch paths and a held
reference oscillator pitch are reported. The latter still includes changes in
the raw controls caused by each path's preceding sampled pitch/context.
This does not evaluate embedded DSP, NPU outputs, playback or block seams.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import zlib

import numpy as np

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(HERE.parent / "dsp"))
from reference_synth import HOP, SAMPLE_RATE, midi_to_hz, render, scale_controls


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def waveform_metrics(actual, reference):
    a, b = np.asarray(actual, np.float64), np.asarray(reference, np.float64)
    if a.ndim != 1 or a.shape != b.shape or not len(a):
        raise ValueError("Expected equal nonempty mono waveforms")
    if not np.isfinite(a).all() or not np.isfinite(b).all():
        raise ValueError("Nonfinite waveform")
    error = a - b
    rms = float(np.sqrt(np.mean(b*b)))
    result = {
        "samples": len(a), "reference_rms": rms,
        "actual_rms": float(np.sqrt(np.mean(a*a))),
        "reference_peak": float(np.max(np.abs(b))),
        "actual_peak": float(np.max(np.abs(a))),
        "rmse": float(np.sqrt(np.mean(error*error))),
        "maximum_absolute_error": float(np.max(np.abs(error))),
        "reference_samples_outside_unit_range": int(np.count_nonzero(np.abs(b) > 1)),
        "actual_samples_outside_unit_range": int(np.count_nonzero(np.abs(a) > 1)),
        "spectra": [],
    }
    result["rmse_over_reference_rms"] = result["rmse"] / rms if rms else None
    for size in (512, 1024, 2048):
        # Include the final partial window using explicit zero padding. Periodic
        # Hann; hop one quarter of FFT size; identical boundaries in both paths.
        hop = size // 4
        padding = max(size - len(a), 0)
        padding += (-(len(a) + padding - size)) % hop
        window = .5 - .5*np.cos(2*np.pi*np.arange(size)/size)
        spectra = []
        for values in (a, b):
            frames = np.lib.stride_tricks.sliding_window_view(np.pad(values, (0, padding)), size)[::hop]
            spectra.append(np.abs(np.fft.rfft(frames*window, axis=-1)))
        candidate, target = spectra
        norm = float(np.linalg.norm(target))
        peak = float(target.max())
        floor = max(peak*1e-5, 1e-12)
        log_error = np.abs(20*np.log10(np.maximum(candidate, floor)) -
                           20*np.log10(np.maximum(target, floor)))
        result["spectra"].append({
            "fft_size": size, "hop": hop, "window": "periodic Hann",
            "spectral_convergence": float(np.linalg.norm(candidate-target)/norm) if norm else None,
            "mean_absolute_log_magnitude_error_db": float(log_error.mean()),
            "log_floor_absolute_magnitude": floor,
            "log_floor_relative_to_reference_peak_db": -100,
        })
    return result


def load_case(directory, instrument):
    manifest_path = directory / f"instrument_{instrument:02}.json"
    arrays_path = directory / f"instrument_{instrument:02}_controls.npz"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest["result"]["instrument_id"] != instrument:
        raise ValueError(f"Instrument identity mismatch: {manifest_path}")
    for name, digest in manifest["provenance"]["file_sha256"].items():
        if sha256(HERE/name) != digest:
            raise ValueError(f"Stale source/model checkpoint: {name}")
    with np.load(arrays_path, allow_pickle=False) as saved:
        values = {name: np.asarray(saved[name], np.float32).copy()
                  for name in ("ptq_raw", "float_raw", "ptq_pitch", "float_pitch")}
    count = manifest["result"]["frames"]
    for name, array in values.items():
        expected = (1, count, 126) if name.endswith("raw") else (1, count, 1)
        if array.shape != expected or not np.isfinite(array).all():
            raise ValueError(f"Invalid {name} shape or values: {array.shape}")
    return manifest, values, {manifest_path.name: sha256(manifest_path), arrays_path.name: sha256(arrays_path)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--controls-dir", type=Path, default=ROOT/".flow/midi_ddsp/closed_loop_batch16")
    parser.add_argument("--output", type=Path, default=HERE/"waveform_diagnostics.json")
    parser.add_argument("--instruments", type=int, nargs="+", default=list(range(13)))
    args = parser.parse_args()
    if not args.instruments or len(set(args.instruments)) != len(args.instruments) or any(i not in range(13) for i in args.instruments):
        parser.error("Expected distinct instrument IDs from 0 through 12")
    asset_path = HERE/"cpu_assets.bin"
    asset = asset_path.read_bytes()
    metadata = json.loads(asset_path.with_suffix(".json").read_text())
    if len(asset) != metadata["bytes"] or zlib.crc32(asset) != metadata["crc32"]:
        raise ValueError("CPU asset size/CRC mismatch")
    record = metadata["tensors"]["s/reverb_module/magnitudes_embedding/embeddings"]
    learned = np.frombuffer(asset, dtype="<f4", count=record["count"], offset=record["offset"]).reshape(13, 48000)
    cases, hashes, provenance = [], {}, None
    for instrument in args.instruments:
        manifest, values, sources = load_case(args.controls_dir, instrument)
        if provenance is not None and manifest["provenance"] != provenance:
            raise ValueError("Mixed model/source provenance in corpus")
        provenance = manifest["provenance"]
        hashes.update(sources)
        raw = values["ptq_raw"][0]
        reference_raw = values["float_raw"][0]
        pitch, reference_pitch = values["ptq_pitch"].reshape(-1), values["float_pitch"].reshape(-1)
        f0, reference_f0 = midi_to_hz(pitch), midi_to_hz(reference_pitch)
        white = np.random.Generator(np.random.PCG64(19100+instrument)).uniform(-1, 1, len(raw)*HOP).astype(np.float32)
        reference = render(reference_raw, reference_f0, white, learned[instrument])
        actual = render(raw, f0, white, learned[instrument])
        held_pitch = render(raw, reference_f0, white, learned[instrument])
        scaled = scale_controls(raw, reference_f0)
        reference_scaled = scale_controls(reference_raw, reference_f0)
        controls = {}
        for name, candidate, target in zip(("amplitude", "harmonic_distribution", "noise_magnitudes"), scaled, reference_scaled):
            delta = candidate.astype(np.float64)-target.astype(np.float64)
            target_rms = float(np.sqrt(np.mean(target.astype(np.float64)**2)))
            rmse = float(np.sqrt(np.mean(delta*delta)))
            controls[name] = {"maximum_absolute_error": float(np.max(np.abs(delta))),
                              "rmse": rmse, "reference_rms": target_rms,
                              "rmse_over_reference_rms": rmse/target_rms if target_rms else None}
        case = {"instrument_id": instrument, "frames": len(raw),
                "same_reference_f0_scaled_controls": controls,
                "independent_pitch_paths": waveform_metrics(actual, reference),
                "held_reference_oscillator_pitch": waveform_metrics(held_pitch, reference)}
        cases.append(case)
        print("Waveform diagnostic instrument", instrument,
              "held-f0 spectral convergence", [x["spectral_convergence"] for x in case["held_reference_oscillator_pitch"]["spectra"]], flush=True)
    report = {"sample_rate_hz": SAMPLE_RATE, "instruments": args.instruments,
              "complete_all_13": set(args.instruments) == set(range(13)),
              "white_noise": "NumPy PCG64 seed 19100+instrument; same uniform[-1,1) realization per comparison",
              "phase": "precise cumulative phase, shared zero initial phase; see reference_synth.py",
              "reverb": "same full signed learned 48000-tap IR with upstream decay and unity dry",
              "limitations": "Diagnostics only, no acceptance gate or perceptual pass. Host independent sampled trajectories; held oscillator pitch retains raw-control path differences. No NPU PCM, firmware DSP, natural demo register, retained tail or block-seam validation. Unit-range counts precede any playback gain.",
              "source_provenance": provenance, "checkpoint_sha256": hashes,
              "file_sha256": {"cpu_assets.bin": sha256(asset_path), "cpu_assets.json": sha256(asset_path.with_suffix(".json")),
                              "reference_synth.py": sha256(HERE.parent/"dsp/reference_synth.py"),
                              "validate_waveforms.py": sha256(Path(__file__))}, "cases": cases}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary = args.output.with_suffix(args.output.suffix+".tmp")
    temporary.write_text(json.dumps(report, indent=2, allow_nan=False)+"\n", encoding="utf-8")
    temporary.replace(args.output)
    print("Waveform diagnostics complete", args.output, flush=True)


if __name__ == "__main__":
    main()
