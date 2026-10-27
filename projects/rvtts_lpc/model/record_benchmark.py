#!/usr/bin/env python3
# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Summarize an uninterrupted CMSIS UART benchmark capture; never access the board."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import statistics

import yaml

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SHAPES = {
    "prosody_pre": ([32, 184], [32, 64]),
    "prosody_gru": ([1, 256], [1, 128]),
    "prosody_head": ([32, 128], [32, 6]),
    "acoustic": ([1, 100, 170], [1, 32, 17]),
}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--recorded-at", required=True)
    parser.add_argument("--build-job", required=True)
    parser.add_argument("--execution-job", required=True)
    parser.add_argument("--firmware", type=Path, default=ROOT / "out/rvtts_lpc/DevKit-E8/Release/rvtts_lpc.axf")
    args = parser.parse_args()
    text = (HERE / "benchmark_uart.txt").read_text()
    assert text.count("benchmark clocks") == 1 and text.count("Test_result: PASS") == 1
    cpu, axi, timer, model_bytes = map(int, re.search(r"CPU=(\d+) AXI=(\d+) timer=(\d+) model=(\d+)", text).groups())
    method_peak, temp_peak, reserved = map(int, re.search(r"Pools method=(\d+) temp=(\d+) reserved=(\d+)", text).groups())
    details = {"recorded_at": args.recorded_at, "methods": {}, "sequences": {},
               "scope": "Feature inference only; excludes text frontend, LPC vocoder, PCM conversion, UART, LCD and playback.",
               "model_sha256": digest(HERE.parent / "ai_layer/model.pte"),
               "firmware_sha256": digest(args.firmware),
               "pools": {"method_peak_bytes": method_peak, "temp_peak_bytes": temp_peak, "reserved_bytes": reserved}}
    for name, error, tolerance, weights, arena in re.findall(r"Method (\w+) error=([\d.]+) tolerance=([\d.]+) weights=(\d+) arena=(\d+)", text):
        details["methods"][name] = dict(max_absolute_error=float(error), tolerance=float(tolerance),
                                          weights_region_bytes=int(weights), arena_region_bytes=int(arena))
    for n, audio_ms, error in re.findall(r"Sequence frames=(\d+) audio_ms=(\d+) max_error=([\d.]+)", text):
        details["sequences"][n] = dict(frames=int(n), audio_ms=int(audio_ms), max_absolute_error=float(error), tolerance=0.01)
    for kind, name, sample, ticks, cycles, invocations, overflow in re.findall(r"(Method|Sequence)Sample (\w+) (\d+) ticks=(\d+) cycles=(\d+) invocations=(\d+) overflow=(\d+)", text):
        record = details["methods" if kind == "Method" else "sequences"][name]
        assert int(sample) == len(record.setdefault("cycles_samples", [])) + 1
        record["cycles_samples"].append(int(cycles))
        record.setdefault("forward_ticks_samples", []).append(int(ticks))
        record["invocations"] = int(invocations)
        assert int(overflow) == 0
    assert set(details["methods"]) == set(SHAPES) and set(details["sequences"]) == {"2", "81", "157"}
    platform = dict(os="RTX5", cpu="Cortex-M55", memory={
        "weights": dict(type="OSPI1", clock_mhz=25, clock_source="RTE/BSP/AE822FA0E5597LS0_M55_HP/ospi_xip_user.h OSPI_CLOCK_MHZ"),
        "activations": dict(type="SRAM0", clock_mhz=axi / 1e6)})
    for kind in ("methods", "sequences"):
        for name, record in details[kind].items():
            cycles, ticks = record["cycles_samples"], record["forward_ticks_samples"]
            assert len(cycles) == len(ticks) == 10
            record["npu_ms"] = statistics.median(cycles) * 1000 / axi
            record["forward_ms"] = statistics.median(ticks) * 1000 / timer
            is_method = kind == "methods"
            if is_method:
                shape, output_shape = SHAPES[name]
                input_bytes = 4
                for dimension in shape:
                    input_bytes *= dimension
                operation = "rvTTS " + name + "; all four methods remain loaded"
                weights, arena = record["weights_region_bytes"], record["arena_region_bytes"]
                scope = "ExecuTorch execute only, including float boundary quantize/dequantize; excludes caller input/output copies and validation."
                source = "Independent held-out quantized method fixture in model_test_data.h; float tolerance is 4 output LSB plus 1e-6 for float arithmetic."
            else:
                n = int(name)
                shape, output_shape = [n, 3], [n, 20]
                input_bytes = n * 4
                operation = f"rvTTS LPC full neural feature schedule, {n} frames; logical phone/stress/word input columns"
                weights = sum(item["weights_region_bytes"] for item in details["methods"].values())
                arena = max(item["arena_region_bytes"] for item in details["methods"].values())
                scope = "Full feature schedule: OSPI embedding lookup, input assembly, paired bidirectional GRU scan, head, halo tiles, state/output copies, finite checks. " + details["scope"]
                source = f"benchmark_inputs.h: {n} frames, seed {dict([(2,800),(81,802),(157,803)])[n]}; phone/stress uint8 and word uint16 arrays; reference supplied int8 weights with float activations."
                record["real_time_factor_features_only"] = record["forward_ms"] / record["audio_ms"]
            network = dict(operation=operation, input_shape=shape, output_shape=output_shape, float_boundary=True,
                           quantization="symmetric int16 activations and supplied per-tensor int8 weights; float graph boundaries")
            if not is_method:
                network.update(samples_per_forward=int(name)*160, sample_rate_hz=16000, block_deadline_ms=record["audio_ms"])
            time = dict(ms=record["npu_ms"], cycles=statistics.median(cycles), statistic="median",
                        min_cycles=min(cycles), max_cycles=max(cycles), mean_cycles=statistics.mean(cycles),
                        forward_ms=record["forward_ms"], forward_timer_hz=timer,
                        cycles_samples=cycles, forward_ticks_samples=ticks)
            memory = dict(model_bytes=model_bytes, input_tensor_bytes=input_bytes,
                          ethos=dict(weights_region_bytes=weights, arena_region_bytes=arena),
                          executorch=dict(method_pool_peak_bytes=method_peak, temp_pool_peak_bytes=temp_peak))
            run = dict(recorded_at=args.recorded_at, context="rvtts_lpc.Release+DevKit-E8", target="DevKit-E8@RvTTS_LPC_Release",
                       device="AE822FA0E5597LS0", core="M55_HP", npu="Ethos-U85", macs=256,
                       compiler="Arm Compiler 6.24.0", optimize="-O3 -ffast-math", build_job=args.build_job,
                       execution_job=args.execution_job, cpu_clock_mhz=cpu/1e6, npu_clock_mhz=axi/1e6,
                       warmup=1, samples=10, pmu_cross_check="CYCLE == NPU_ACTIVE for every invocation; summed for complete schedules",
                       pmu_overflow=0, npu_invocations_per_forward=record["invocations"], test_result="PASS",
                       max_absolute_error=record["max_absolute_error"], tolerance=record["tolerance"],
                       uart="COM5 115200 8N1 via CMSIS Developer Assistant serial tools", model_sha256=details["model_sha256"],
                       firmware_sha256=details["firmware_sha256"], input=source, timing_scope=scope,
                       uart_run="benchmark_uart.txt", measurement_source="CMSIS load_and_run r-9; firmware PMU and osKernelGetSysTimerCount; raw samples in benchmark_uart.txt")
            document = {"benchmarks": dict(network=network, benchmark=[dict(platform=platform, measurements=dict(memory=memory,time=time),run=run)])}
            filename = "benchmark.yml" if name == "157" else "benchmark_" + name + ".yml"
            (HERE / filename).write_text("# Measured on Alif DevKit-E8; no template data or compiler time estimates.\n" + yaml.safe_dump(document, sort_keys=False), encoding="utf-8")
    (HERE / "benchmark_details.json").write_text(json.dumps(details, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
