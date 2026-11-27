# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Replay 24 kHz audio over SDS USB and compare the board capture to its input."""

import argparse
import json
from pathlib import Path
import shutil
from struct import Struct
import subprocess
import sys
from tempfile import mkdtemp
import wave

import numpy as np


RATE = 24_000
DEFAULT_RECORD_SAMPLES = 2880
HEADER = Struct("<II")
FLOAT32 = np.dtype("<f4")
FIXTURE_DIR = Path(__file__).resolve().parent
WORKDIR = FIXTURE_DIR / "sds_run"
CONTROL = FIXTURE_DIR / "datatest.sdsio.yml"
REPLAY = "Encodec_In.0.sds"
CAPTURE = "Encodec_Out.0.p.sds"
MANIFEST = "fixture.json"


def test_signal():
    time = np.arange(3 * RATE, dtype=np.float64) / RATE
    envelope = np.sin(np.pi * time / 3) ** 2
    harmonics = sum(np.sin(2 * np.pi * frequency * time) / (index + 1)
                    for index, frequency in enumerate((173, 346, 519)))
    sweep = np.sin(2 * np.pi * (250 * time + 450 * time**2))
    audio = envelope * (0.22 * harmonics + 0.18 * sweep)
    audio[RATE:RATE + RATE // 8] = 0
    return audio.astype(FLOAT32)


def read_wav(path):
    with wave.open(str(path), "rb") as source:
        if (source.getnchannels(), source.getsampwidth(), source.getframerate()) != (1, 2, RATE):
            raise ValueError(f"{path} must be mono 24 kHz PCM16")
        samples = np.frombuffer(source.readframes(source.getnframes()), "<i2")
        if not samples.size:
            raise ValueError(f"{path} contains no audio samples")
    return samples.astype(FLOAT32) / 32768


def timeslot(index, record_samples):
    return index * record_samples * 1_000 // RATE


def write_sds(path, samples, record_samples):
    with path.open("wb") as target:
        for index, block in enumerate(samples.reshape(-1, record_samples)):
            payload = block.astype(FLOAT32, copy=False).tobytes()
            target.write(HEADER.pack(timeslot(index, record_samples), len(payload)))
            target.write(payload)


def read_sds(path, records, record_samples):
    record_size = record_samples * FLOAT32.itemsize
    samples = np.empty((records, record_samples), dtype=FLOAT32)
    with path.open("rb") as source:
        for index in range(records):
            header = source.read(HEADER.size)
            if len(header) != HEADER.size:
                raise ValueError(f"{path.name}: missing or truncated header at record {index}")
            slot, size = HEADER.unpack(header)
            if slot != timeslot(index, record_samples) or size != record_size:
                raise ValueError(f"{path.name}: unexpected timeslot {slot} or payload size {size} at record {index}")
            payload = source.read(size)
            if len(payload) != size:
                raise ValueError(f"{path.name}: truncated payload at record {index}")
            samples[index] = np.frombuffer(payload, dtype=FLOAT32)
        if source.read(1):
            raise ValueError(f"{path.name}: unexpected data after {records} records")
    if not np.isfinite(samples).all():
        raise ValueError(f"{path.name}: nonfinite audio samples")
    return samples.ravel()


def prepare(workdir, input_wav, record_samples, delay_samples):
    if record_samples <= 0 or delay_samples < 0:
        raise ValueError("record size must be positive and delay must be nonnegative")
    reference = read_wav(input_wav) if input_wav else test_signal()
    records = (reference.size + delay_samples + record_samples - 1) // record_samples
    replay = np.pad(reference, (0, records * record_samples - reference.size))
    manifest = dict(samples=reference.size, records=records,
                    record_samples=record_samples, delay_samples=delay_samples)
    workdir.mkdir(parents=True, exist_ok=True)
    write_sds(workdir / REPLAY, replay, record_samples)
    (workdir / MANIFEST).write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Prepared {reference.size} input samples in {records} records at {RATE} Hz", flush=True)


def compare(workdir, capture_path):
    manifest = json.loads((workdir / MANIFEST).read_text(encoding="utf-8"))
    count = manifest["samples"]
    delay = manifest["delay_samples"]
    records = manifest["records"]
    record_samples = manifest["record_samples"]
    if min(count, records, record_samples) <= 0 or not 0 <= delay <= records * record_samples - count:
        raise ValueError("fixture manifest has inconsistent audio lengths")
    reference = read_sds(workdir / REPLAY, records, record_samples)[:count].astype(np.float64)
    capture = read_sds(capture_path or workdir / CAPTURE, records, record_samples)
    error = capture[delay:delay + count] - reference
    signal_power = np.mean(reference**2)
    noise_power = np.mean(error**2)
    snr = 10 * np.log10(signal_power / noise_power) if signal_power and noise_power else None
    print(f"Compared {count} samples across {records} records at {delay} samples of alignment")
    print(f"Mean absolute error {np.mean(np.abs(error)):.8g}")
    print(f"Peak absolute error {np.max(np.abs(error)):.8g}")
    print(f"RMSE {np.sqrt(noise_power):.8g}")
    if snr is not None:
        print(f"SNR {snr:.3f} dB")
    elif not noise_power:
        print("The aligned samples match exactly")


def run_replay(workdir, input_wav, delay_samples, server_path, timeout_seconds):
    if timeout_seconds <= 0:
        raise ValueError("--timeout-seconds must be positive")
    if server_path is None:
        executable = shutil.which("sdsio-server")
        if executable is None:
            raise ValueError("sdsio-server is missing from PATH. Pass --sds-server with the SDK path.")
        server = [executable]
    else:
        server_path = server_path.resolve()
        if not server_path.is_file():
            raise ValueError(f"SDSIO Server file is missing at {server_path}")
        server = [str(server_path)]
        if server_path.suffix.lower() == ".py":
            server = [sys.executable, "-u", *server]

    workdir = workdir.resolve()
    workdir.mkdir(parents=True, exist_ok=True)
    run_dir = Path(mkdtemp(prefix="run-", dir=workdir))
    print(f"SDS run directory {run_dir}", flush=True)
    prepare(run_dir, input_wav, DEFAULT_RECORD_SAMPLES, delay_samples)
    try:
        subprocess.run(
            [*server, "--control", str(CONTROL), "--workdir", str(run_dir),
             "--playback", "--exit-after-playback", "--no-progress-info"],
            stdin=subprocess.DEVNULL, check=True, timeout=timeout_seconds,
        )
    except subprocess.CalledProcessError as error:
        raise ValueError(f"SDSIO Server exited with status {error.returncode}") from error
    except subprocess.TimeoutExpired as error:
        raise ValueError(
            f"SDS USB replay timed out after {timeout_seconds} seconds. "
            "Check that EnCodec_Demo is running and the MCU USB Device port is connected."
        ) from error
    compare(run_dir, None)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workdir", type=Path, default=WORKDIR, help="base directory for SDS fixtures")
    commands = parser.add_subparsers(dest="action", required=True)
    prepare_command = commands.add_parser("prepare", help="write the replay and manifest")
    run_command = commands.add_parser("run", help="prepare, replay over USB, capture and compare")
    for replay_command in (prepare_command, run_command):
        replay_command.add_argument("--input-wav", type=Path, help="24 kHz mono PCM16 WAV")
        replay_command.add_argument("--delay-samples", type=int, default=0,
                                    help="align the capture by this many samples")
    run_command.add_argument("--sds-server", type=Path,
                             help="SDSIO Server script or executable (default: sdsio-server on PATH)")
    run_command.add_argument("--timeout-seconds", type=int, default=60,
                             help="maximum time for the SDS replay (default: 60)")
    compare_command = commands.add_parser("compare", help="measure the recorded reconstruction")
    compare_command.add_argument("--capture", type=Path)
    args = parser.parse_args()
    try:
        if args.action == "prepare":
            prepare(args.workdir, args.input_wav, DEFAULT_RECORD_SAMPLES, args.delay_samples)
        elif args.action == "run":
            run_replay(args.workdir, args.input_wav, args.delay_samples,
                       args.sds_server, args.timeout_seconds)
        else:
            compare(args.workdir, args.capture)
    except (OSError, ValueError, wave.Error) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()
