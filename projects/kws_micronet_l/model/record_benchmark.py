#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Verify the captured board outputs and record measured and estimated results."""
import argparse
import csv
from datetime import datetime, timezone
import importlib.metadata
import json
from pathlib import Path
import re
import statistics
import sys

import numpy as np
import yaml

ROOT = Path(__file__).resolve().parents[3]
MODEL = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / "scripts"))
from quantized_model import sha256
from validate_networks import load_network


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-job", required=True)
    parser.add_argument("--execution-job", required=True)
    args = parser.parse_args()
    raw = (MODEL / "benchmark_uart.txt").read_text(encoding="utf-8")
    runs = [part for part in raw.split("KWS network-only test:")[1:] if "Test_result: PASS" in part]
    assert runs, "No complete passing board run in the UART capture"
    text = runs[-1]
    fixtures = json.loads((MODEL / "board_test_vectors.json").read_text())
    header = (MODEL / "model_test_data.h").read_text()
    assert sha256(MODEL / "model_test_data.h") == fixtures["header_sha256"]
    for key, filename in (("source_sha256", "kws_micronet_l.tflite"),
                          ("graph_sha256", "model_quantized.pt2"),
                          ("safetensors_sha256", "model_quantized.safetensors"),
                          ("generator_sha256", "generate_board_tests.py")):
        assert sha256(MODEL / filename) == fixtures[key], filename

    def expected(name):
        body = re.search(r"static const int8_t " + name + r"\[\d+\]\[\d+\] = \{(.*?)\n\};", header, re.S)[1]
        return np.array(list(map(int, re.findall(r"-?\d+", body)))).reshape(270, 12)

    rows = re.findall(r"KWS output (\d+):([^\r\n]+)", text)
    assert [int(row[0]) for row in rows] == list(range(270)), "Incomplete or duplicate board outputs"
    actual = np.array([list(map(int, row[1].split())) for row in rows])
    assert actual.shape == (270, 12) and actual.min() >= -128 and actual.max() <= 127
    comparisons = {}
    for name in ("default", "reference", "pytorch"):
        ref = expected("kws_pytorch" if name == "pytorch" else "kws_tflite_" + name)
        delta = np.abs(actual-ref)
        mismatch = np.flatnonzero(actual.argmax(axis=1) != ref.argmax(axis=1))
        comparisons[name] = {"max_error_lsb": int(delta.max()), "mean_error_lsb": float(delta.mean()),
                             "exact_output_fraction": float((delta == 0).mean()),
                             "top1_agreement": float(1-len(mismatch)/270),
                             "top1_difference_patterns": [fixtures["patterns"][i] for i in mismatch],
                             "top1_differences": [{"pattern": fixtures["patterns"][i],
                                                   "board_scores": actual[i].tolist(),
                                                   "expected_scores": ref[i].tolist()}
                                                  for i in mismatch]}
        assert delta.max() <= fixtures["tolerance_lsb"], (name, comparisons[name])
    reported = tuple(map(int, re.search(
        r"KWS accuracy: cases=(\d+), default max=(\d+) LSB, reference max=(\d+) LSB, pytorch max=(\d+) LSB, tolerance=(\d+) LSB", text).groups()))
    assert reported == (270, *(comparisons[name]["max_error_lsb"] for name in ("default", "reference", "pytorch")), fixtures["tolerance_lsb"])
    samples = re.findall(r"Benchmark sample (\d+): NPU=(\d+) cycles \(([\d.]+) ms\), forward=(\d+) ticks \(([\d.]+) ms\)", text)
    assert [int(s[0]) for s in samples] == list(range(100)), "Incomplete timing samples"
    cycles, ticks = [int(s[1]) for s in samples], [int(s[3]) for s in samples]
    cpu_hz, npu_hz, timer_hz = map(int, re.search(r"Benchmark clocks: CPU=(\d+) Hz, AXI/NPU=(\d+) Hz, timer=(\d+) Hz", text).groups())
    model_bytes, arena_bytes, arena_address, weights_bytes, weights_address = re.search(
        r"Benchmark memory: model=(\d+) B, arena=(\d+) B at (0x[0-9a-f]+), weights=(\d+) B at (0x[0-9a-f]+)", text).groups()
    method_peak, temp_peak, _ = map(int, re.search(r"Benchmark pools: method peak=(\d+) B, temp peak=(\d+) B, reserved=(\d+) B", text).groups())
    active, invocations, overflow = map(int, re.search(r"Benchmark PMU: last active=(\d+) cycles, invocations=(\d+), overflow=(\d+)", text).groups())
    assert active == cycles[-1] and invocations == 1 and overflow == 0 and min(cycles) > 0
    for sample in samples:
        assert abs(float(sample[2])-1000*int(sample[1])/npu_hz) <= .000001
        assert abs(float(sample[4])-1000*int(sample[3])/timer_hz) <= .000001
    generation = json.loads((MODEL.parent / "ai_layer/generation.json").read_text())
    assert generation["project"] == "projects/kws_micronet_l/kws_micronet_l.cproject.yml"
    assert generation["delegates"] == 1 and generation["operators"] == []
    assert generation["pte_sha256"] == sha256(MODEL.parent / "ai_layer/model.pte") and int(model_bytes) == generation["pte_bytes"]
    assert generation["safetensors_sha256"] == fixtures["safetensors_sha256"]
    firmware = ROOT / "out/kws_micronet_l/DevKit-E8/Release/kws_micronet_l.axf"
    build = yaml.safe_load(firmware.with_name("kws_micronet_l.Release+DevKit-E8.cbuild.yml").read_text())["build"]
    recorded_at = datetime.now(timezone.utc).isoformat()
    detail = {"recorded_at": recorded_at, "patterns": 270, "outputs_checked": int(actual.size),
              "comparisons": comparisons, "tolerance_lsb": fixtures["tolerance_lsb"],
              "fixture_sha256": fixtures["header_sha256"], "uart_sha256": sha256(MODEL / "benchmark_uart.txt"),
              "pte_sha256": generation["pte_sha256"], "firmware_elf_sha256": sha256(firmware),
              "harness_sha256": sha256(MODEL.parent / "app_main.cpp"),
              "project_sha256": sha256(MODEL.parent / "kws_micronet_l.cproject.yml"),
              "npu_weights_address": weights_address, "npu_arena_address": arena_address,
              "timing_samples": 100, "scope": "Network only, synthetic features; no speech accuracy, preprocessing, microphone or LCD"}
    (MODEL / "board_validation.json").write_text(json.dumps(detail, indent=2)+"\n", encoding="utf-8")
    with (MODEL / "benchmark_vela_summary.csv").open(newline="") as file:
        estimate = next(csv.DictReader(file))
    config = load_network(MODEL / "network.yml")["vela"]
    bandwidth = {}
    for key, memory, prefix in (("sram", "SRAM0", "sram"), ("weights", "MRAM", "off_chip_flash")):
        total = int(estimate[prefix+"_total_bytes"])
        bandwidth[key] = {"memory": memory, "vela_memory_area": prefix,
                          "total_bytes_per_inference": total//int(estimate["batch_size"]),
                          "average_bytes_per_second": total/float(estimate["inference_time"]),
                          "design_peak_bytes_per_second": float(estimate[prefix+"_bandwidth"])*2**30}
    benchmark = {
        "platform": {"os": "RTX5", "os_version": "5.9.1", "cpu": "Cortex-M55",
                     "memory": {"weights": {"type": "MRAM"}, "activations": {"type": "SRAM0", "clock_mhz": npu_hz/1e6}}},
        "measurements": {
            "memory": {"model_bytes": int(model_bytes), "input_tensor_bytes": 490,
                       "ethos": {"weights_region_bytes": int(weights_bytes), "arena_region_bytes": int(arena_bytes)},
                       "executorch": {"method_pool_peak_bytes": method_peak, "temp_pool_peak_bytes": temp_peak}},
            "time": {"ms": 1000*statistics.median(cycles)/npu_hz, "cycles": statistics.median(cycles),
                     "statistic": "median", "min_cycles": min(cycles), "max_cycles": max(cycles),
                     "mean_cycles": statistics.mean(cycles), "forward_ms": 1000*statistics.median(ticks)/timer_hz,
                     "forward_timer_hz": timer_hz, "cycles_samples": cycles, "forward_ticks_samples": ticks}},
        "estimates": {"vela": {**config, "version": importlib.metadata.version("ethos-u-vela"),
                               "accelerator_config": estimate["accelerator_configuration"],
                               "model_sha256": generation["pte_sha256"], "ini_sha256": sha256(MODEL.parent/config["ini"]),
                               "summary": "benchmark_vela_summary.csv", "cycles": int(estimate["cycles_total"]),
                               "time_ms": 1000*float(estimate["inference_time"]), "bandwidth": bandwidth}},
        "run": {"recorded_at": recorded_at, "context": build["context"], "target": "DevKit-E8@KWS_Release",
                "device": "AE822FA0E5597LS0", "core": "M55_HP", "npu": "Ethos-U85", "macs": 256,
                "packs": {"driver": "ARM::CMSIS-Ethos-U@1.0.0", "driver_component": "ARM::Machine Learning:NPU Support:Ethos-U Driver&Generic U85@2.0.0",
                          "board": "AlifSemiconductor::Ensemble@2.2.1", "runtime": "PyTorch::ExecuTorch@1.4.1"},
                "compiler": "AC6@6.24.0", "optimize": "-O3 -ffast-math", "build_job": args.build_job,
                "execution_job": args.execution_job, "cpu_clock_mhz": cpu_hz/1e6, "npu_clock_mhz": npu_hz/1e6,
                "warmup": 1, "samples": 100, "pmu_cross_check": "PMCCNTR equals PMEVCNTR0 (NPU_ACTIVE) on every inference",
                "pmu_overflow": 0, "npu_invocations_per_forward": 1, "test_result": "PASS",
                "max_absolute_error": max(c["max_error_lsb"] for c in comparisons.values()), "tolerance": fixtures["tolerance_lsb"],
                "uart": "benchmark_uart.txt", "model_sha256": generation["pte_sha256"], "firmware_sha256": sha256(firmware),
                "input": "270 locally generated MFCC-shaped int8 inputs (seed 20261016), all 12 scores checked against original TFLite reference/default and PyTorch; benchmark repeats random_0",
                "timing_scope": "module.forward() only; includes ExecuTorch and PMU hooks; excludes tensor setup, input/output copy, validation and UART; after 270 validation forwards and one immediate warmup; no debugger halt during measured loop",
                "uart_run": "last complete startup in benchmark_uart.txt", "measurement_source": "UART4 COM5 115200 8N1; Ethos PMU, RTOS system timer and allocator high-water marks"}}
    network = {"operation": "MicroNet Large keyword spotting: 16 convolutions including depthwise, ReLU6, average pool, 12 class scores (no softmax)",
               "input_shape": [1,49,10,1], "output_shape": [1,12], "float_boundary": False,
               "quantization": "Original TFLite asymmetric int8 activations and per-channel int8 weights, int32 bias; int8 input/output; error units are raw int8 score steps"}
    document = {"benchmarks": {"network": network, "benchmark": [benchmark]}}
    (MODEL / "benchmark.yml").write_text(yaml.safe_dump(document, sort_keys=False), encoding="utf-8")
    print(json.dumps({"accuracy": comparisons, "median_npu_ms": benchmark["measurements"]["time"]["ms"],
                      "median_forward_ms": benchmark["measurements"]["time"]["forward_ms"],
                      "network_memory": benchmark["measurements"]["memory"]}, indent=2))


if __name__ == "__main__":
    main()
