# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Prepare recorded DTLN fixtures and compare captured SDS against float DTLN-128."""

import argparse
import json
from math import log10
from pathlib import Path
from struct import Struct
import wave

import numpy as np

HERE = Path(__file__).resolve().parent
RATE = 16000
HOP = 128
WINDOW = 512
DELAY = WINDOW - HOP
PERIOD_MS = HOP * 1000 // RATE
HEADER = Struct("<II")
CASES = (
    ("far-end", "9mkQhVtzTEy2hDk-6u2Sww_farend_singletalk"),
    ("near-end", "DLhjtuwiEkS-68TsUVvW5g_nearend_singletalk"),
    ("double-talk", "DMTgmZwtgUilp4omPK7-OQ_doubletalk"),
)


def read_wav(path):
    with wave.open(str(path), "rb") as source:
        if (source.getnchannels(), source.getsampwidth(), source.getframerate()) != (1, 2, RATE):
            raise ValueError(f"{path} must be mono 16 kHz PCM16")
        return np.frombuffer(source.readframes(source.getnframes()), "<i2").astype(np.float32) / 32768


def write_wav(path, data):
    pcm = np.clip(np.rint(data * 32768), -32768, 32767).astype("<i2")
    with wave.open(str(path), "wb") as target:
        target.setparams((1, 2, RATE, 0, "NONE", "not compressed"))
        target.writeframes(pcm.tobytes())


def write_sds(path, samples):
    with path.open("wb") as target:
        for index, block in enumerate(samples.reshape(-1, HOP, samples.shape[1])):
            data = block.astype("<f4", copy=False).tobytes()
            target.write(HEADER.pack(index * PERIOD_MS, len(data)))
            target.write(data)


def read_capture(path, records):
    size = HOP * np.dtype("<f4").itemsize
    capture = np.empty((records, HOP), np.float32)
    with path.open("rb") as source:
        for index in range(records):
            header = source.read(HEADER.size)
            if len(header) != HEADER.size:
                raise ValueError(f"{path.name} ended at record {index}, expected {records} records")
            timeslot, length = HEADER.unpack(header)
            if timeslot != index * PERIOD_MS or length != size:
                raise ValueError(f"{path.name} at record {index} has timeslot {timeslot} and {length} bytes")
            data = source.read(length)
            if len(data) != length:
                raise ValueError(f"{path.name} has a truncated payload at record {index}")
            capture[index] = np.frombuffer(data, "<f4")
        if source.read(1):
            raise ValueError(f"{path.name} contains data after the expected {records} records")
    if not np.isfinite(capture).all():
        raise ValueError(f"{path.name} contains nonfinite samples")
    return capture.reshape(-1)


class ReferenceStage:
    def __init__(self, interpreter):
        self.interpreter = interpreter
        interpreter.allocate_tensors()
        inputs = interpreter.get_input_details()
        outputs = interpreter.get_output_details()
        self.inputs = [detail["index"] for detail in inputs]
        self.outputs = [detail["index"] for detail in outputs]
        self.state = np.zeros(inputs[1]["shape"], dtype=np.float32)

    def run(self, signal, loopback):
        for index, value in zip(self.inputs, (signal, self.state, loopback)):
            self.interpreter.set_tensor(index, value.astype(np.float32, copy=False))
        self.interpreter.invoke()
        result, self.state = (self.interpreter.get_tensor(index) for index in self.outputs)
        return result


def float_reference(samples):
    from tensorflow import lite

    upstream = HERE.parent / "model" / "upstream"
    stages = [
        ReferenceStage(lite.Interpreter(
            model_path=str(upstream / f"dtln_aec_128_{number}.tflite"),
            experimental_op_resolver_type=lite.experimental.OpResolverType.BUILTIN_WITHOUT_DEFAULT_DELEGATES,
        ))
        for number in (1, 2)
    ]
    windowed = np.pad(samples, ((DELAY, 0), (0, 0)))
    result = np.zeros(windowed.shape[0], dtype=np.float32)
    for offset in range(0, len(samples), HOP):
        region = slice(offset, offset + WINDOW)
        microphone, loopback = windowed[region].T
        spectrum = np.fft.rfft(microphone).astype(np.complex64)
        ref_spectrum = np.fft.rfft(loopback).astype(np.complex64)
        mask = stages[0].run(np.abs(spectrum).reshape(1, 1, -1), np.abs(ref_spectrum).reshape(1, 1, -1))
        estimate = np.fft.irfft(spectrum * mask.reshape(-1)).reshape(1, 1, WINDOW)
        enhanced = stages[1].run(estimate, loopback.reshape(1, 1, WINDOW))
        result[region] += enhanced.reshape(-1)
    return result[:len(samples)]


def prepare(audio_dir, workdir):
    workdir.mkdir(parents=True, exist_ok=True)
    manifest = {}
    for label, (case, stem) in enumerate(CASES):
        microphone = read_wav(audio_dir / f"{stem}_mic.wav")
        loopback = read_wav(audio_dir / f"{stem}_lpb.wav")
        length = min(len(microphone), len(loopback))
        if length == 0:
            raise ValueError(f"{case} inputs are empty")
        pair = np.column_stack((microphone[:length], loopback[:length]))
        padded = np.pad(pair, ((DELAY, (-length % HOP) + DELAY), (0, 0)))
        alignment = 2 * DELAY
        reference = float_reference(padded)[alignment:alignment + length]
        if not np.isfinite(reference).all():
            raise ValueError(f"{case} reference contains nonfinite samples")
        write_sds(workdir / f"Dtln_In.{label}.sds", padded)
        np.save(workdir / f"{case}.reference.npy", reference)
        write_wav(workdir / f"{case}.reference.wav", reference)
        manifest[case] = {"label": label, "samples": length, "records": len(padded) // HOP,
                          "capture_offset": alignment,
                          "input_rms": float(np.sqrt(np.mean(np.square(pair[:, 0]), dtype=np.float64)))}
        print(f"{case}: {length} samples, {len(padded) // HOP} SDS records")
    (workdir / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


def level_change_db(output_rms, input_rms):
    if input_rms == 0:
        return "n/a"
    if output_rms == 0:
        return "-inf"
    return f"{20 * log10(output_rms / input_rms):+.2f}"


def print_summary(results):
    print("Reference: original float DTLN-aec 128 (TFLite)")
    print("dB columns show RMS level change from the input microphone.")
    print(f"{'Clip':<12} {'Board dB':>9} {'Float dB':>9} {'MAE':>11} {'RMSE':>11} {'Peak error':>11}")
    for name, metrics in results.items():
        board_db = level_change_db(metrics["output_rms"], metrics["input_rms"])
        float_db = level_change_db(metrics["reference_rms"], metrics["input_rms"])
        print(f"{name:<12} {board_db:>9} {float_db:>9}"
              f" {metrics['mae']:>11.5g} {metrics['rmse']:>11.5g} {metrics['max_abs_error']:>11.5g}")


def compare(workdir, case, max_mae, *, summary=False):
    manifest = json.loads((workdir / "manifest.json").read_text(encoding="utf-8"))
    results = {}
    for name in (manifest if case is None else (case,)):
        spec = manifest[name]
        capture = read_capture(workdir / f"Dtln_Out.{spec['label']}.p.sds", spec["records"])
        offset, length = spec["capture_offset"], spec["samples"]
        output = capture[offset:offset + length]
        reference = np.load(workdir / f"{name}.reference.npy", allow_pickle=False)
        if output.shape != reference.shape or len(output) != length:
            raise ValueError(f"{name} reference and aligned capture lengths differ")
        if not np.isfinite(reference).all():
            raise ValueError(f"{name} reference contains nonfinite samples")
        difference = output.astype(np.float64) - reference
        results[name] = {
            "samples": length,
            "input_rms": spec["input_rms"],
            "reference_rms": float(np.sqrt(np.mean(np.square(reference), dtype=np.float64))),
            "output_rms": float(np.sqrt(np.mean(np.square(output), dtype=np.float64))),
            "mae": float(np.mean(np.abs(difference))),
            "rmse": float(np.sqrt(np.mean(difference ** 2))),
            "max_abs_error": float(np.max(np.abs(difference))),
        }
        write_wav(workdir / f"{name}.capture.wav", output)
    if summary:
        print_summary(results)
    else:
        print(json.dumps(results, indent=2))
    return int(max_mae is not None and any(metrics["mae"] > max_mae for metrics in results.values()))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workdir", type=Path, default=HERE / "sds_run")
    commands = parser.add_subparsers(dest="command", required=True)
    prep = commands.add_parser("prepare")
    prep.add_argument("--audio-dir", type=Path, default=HERE / "audio_samples")
    comparison = commands.add_parser("compare")
    comparison.add_argument("--case", choices=[name for name, _ in CASES])
    comparison.add_argument("--max-mae", type=float)
    comparison.add_argument("--summary", action="store_true", help="print a table of waveform errors and RMS changes")
    args = parser.parse_args()
    try:
        if args.command == "prepare":
            prepare(args.audio_dir, args.workdir)
            return 0
        if args.max_mae is not None and (not np.isfinite(args.max_mae) or args.max_mae < 0):
            parser.error("--max-mae must be finite and nonnegative")
        return compare(args.workdir, args.case, args.max_mae, summary=args.summary)
    except (OSError, ValueError) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
