# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Measure echo cancellation on six contiguous repeats of the demo pattern."""

import argparse
import json
import sys

import numpy as np

from sds_demo import DELAY, HERE, HOP, RATE, float_reference, read_capture, write_sds, write_wav

sys.path.insert(0, str(HERE.parent / "model"))
from test_patterns import MODES, patterns


WORKDIR = HERE / "sds_run" / "synthetic"
REPEATS = 6
TRIM = RATE // 4


def rms(samples):
    samples = np.asarray(samples, dtype=np.float64)
    return float(np.sqrt(np.mean(samples**2)))


def ratio_db(numerator, denominator):
    floor = 1e-12
    return float(20 * np.log10(max(numerator, floor) / max(denominator, floor)))


def prepare(microphone, far):
    WORKDIR.mkdir(parents=True, exist_ok=True)
    pair = np.column_stack((microphone, far))
    padded = np.pad(pair, ((DELAY, -len(pair) % HOP + DELAY), (0, 0)))
    reference = float_reference(padded)[2 * DELAY:2 * DELAY + len(pair)]
    if not np.isfinite(reference).all():
        raise ValueError("The float reference contains nonfinite samples")
    write_sds(WORKDIR / "Dtln_In.0.sds", padded)
    np.save(WORKDIR / "reference.npy", reference)
    write_wav(WORKDIR / "reference.wav", reference)
    print(f"Prepared {len(padded) // HOP} hops with continuous recurrent state")


def measure(signal, microphone, near):
    results = []
    for repeat in range(len(near) // (len(MODES) * RATE)):
        episode = {}
        for section, mode in enumerate(MODES):
            start = (repeat * len(MODES) + section) * RATE
            region = slice(start + TRIM, start + RATE - TRIM)
            output_rms = rms(signal[region])
            residual_rms = rms(signal[region] - near[region])
            values = {"output_rms": output_rms, "error_to_clean_rms": residual_rms}
            if mode == "echo only":
                values["erle_db"] = ratio_db(rms(microphone[region]), output_rms)
            elif mode == "double talk":
                values["error_reduction_db"] = ratio_db(rms(microphone[region] - near[region]), residual_rms)
            elif mode == "near end only":
                values["near_end_gain_db"] = ratio_db(output_rms, rms(near[region]))
            episode[f"{section}_{mode}"] = values
        results.append(episode)
    return results


def compare(microphone, near):
    records = (len(microphone) + 2 * DELAY + HOP - 1) // HOP
    capture = read_capture(WORKDIR / "Dtln_Out.0.p.sds", records)
    output = capture[2 * DELAY:2 * DELAY + len(microphone)]
    reference = np.load(WORKDIR / "reference.npy", allow_pickle=False)
    if reference.shape != output.shape or not np.isfinite(reference).all():
        raise ValueError("The float reference does not match the capture length or contains nonfinite samples")
    results = {
        "records": records,
        "seconds": len(output) / RATE,
        "samples_per_measurement": RATE - 2 * TRIM,
        "mae_vs_float": float(np.mean(np.abs(output.astype(np.float64) - reference))),
        "board": measure(output, microphone, near),
        "float": measure(reference, microphone, near),
    }
    write_wav(WORKDIR / "board.wav", output)
    report = json.dumps(results, indent=2) + "\n"
    (WORKDIR / "metrics.json").write_text(report, encoding="utf-8")
    for index, (board, original) in enumerate(zip(results["board"], results["float"]), start=1):
        print(
            f"Repeat {index}: "
            f"echo-only ERLE {board['1_echo only']['erle_db']:.2f}/{original['1_echo only']['erle_db']:.2f} dB, "
            f"double-talk error reduction "
            f"{board['2_double talk']['error_reduction_db']:.2f}/{original['2_double talk']['error_reduction_db']:.2f} dB, "
            f"near-end gain {board['3_near end only']['near_end_gain_db']:.2f}/"
            f"{original['3_near end only']['near_end_gain_db']:.2f} dB (board/float)"
        )
    return results


def check_quality(results):
    """Guard the continuous demo against the float reference on this fixture."""
    metrics = (
        ("1_echo only", "erle_db", "echo-only ERLE", 1.0),
        ("2_double talk", "error_reduction_db", "double-talk error reduction", 1.0),
        ("3_near end only", "near_end_gain_db", "near-end gain", 2.0),
    )
    for repeat, (board, original) in enumerate(zip(results["board"], results["float"]), start=1):
        for section, key, label, tolerance in metrics:
            delta = board[section][key] - original[section][key]
            if delta < -tolerance or (key == "near_end_gain_db" and delta > tolerance):
                raise ValueError(f"Repeat {repeat}: {label} differs by {delta:+.2f} dB (board minus float)")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("prepare", "compare"))
    parser.add_argument("--check", action="store_true", help="Check signal quality against the float reference.")
    args = parser.parse_args()
    if args.check and args.action != "compare":
        parser.error("--check requires compare")
    microphone, far, near = (np.tile(signal, REPEATS) for signal in patterns())
    if args.action == "prepare":
        prepare(microphone, far)
    else:
        try:
            results = compare(microphone, near)
            if args.check:
                check_quality(results)
        except (OSError, ValueError) as error:
            parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()
