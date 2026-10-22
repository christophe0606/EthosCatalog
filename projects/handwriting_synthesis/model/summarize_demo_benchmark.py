#!/usr/bin/env python3
# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Regenerate the 100 MHz record from preserved CMSIS serial/debug evidence."""
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import re
import statistics

import yaml

HERE = Path(__file__).resolve().parent
METHODS = ("lstm1", "attention", "lstm2", "lstm3", "gmm")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def payload(value):
    """Strip serial-tool envelopes without altering the actual UART payload."""
    if isinstance(value, list):
        return "".join(payload(part) for part in value)
    blocks = value.get("content", [])
    return "".join(block["text"].split("--- text ---\n", 1)[1]
                   for block in blocks if block.get("type") == "text"
                   and "--- text ---\n" in block["text"])


def main():
    provenance_path = HERE / "benchmark_100mhz_provenance.json"
    provenance = json.loads(provenance_path.read_text(encoding="utf-8"))
    startup = payload(json.loads((HERE / provenance["startup_uart"]).read_text(encoding="utf-8")))
    profile = payload(json.loads((HERE / provenance["profile_uart"]).read_text(encoding="utf-8")))
    raw = startup + profile
    require("FAILED" not in raw and "E:" not in raw, "capture contains an error")
    require(raw.count("Handwriting READY.") == 1, "expected one complete startup")
    clock = re.search(r"OSPI read clock=(\d+) Hz AXI=(\d+) Hz divider=(\d+); model CRC=([0-9a-f]+) PASS", raw)
    require(clock is not None, "missing verified clock and model CRC")
    require(int(clock[1]) == provenance["ospi_read_clock_hz"] == int(clock[2]) // int(clock[3]),
            "effective clock disagrees with provenance/divider")
    model = HERE.parent / "ai_layer/model.pte"
    require(hashlib.sha256(model.read_bytes()).hexdigest() == provenance["model_sha256"],
            "PTE differs from the measured model")
    model_size = re.search(r"^Model=(\d+) bytes;", raw, re.MULTILINE)
    require(model_size is not None and int(model_size[1]) == model.stat().st_size, "model size mismatch")
    cpu = re.search(r"CPU fixture: max_error=([\d.]+) tolerance=([\d.]+)", raw)
    require(cpu is not None and float(cpu[1]) <= float(cpu[2]), "CPU fixture did not pass")
    regions = []
    for method in METHODS:
        fixture = re.search(rf"Startup {method} PASS max_error=([\d.]+) NPU=1", raw)
        region = re.search(rf"^REGION {method} weights=(\d+) weights_address=(0x[0-9a-f]+) "
                           r"arena=(\d+) arena_address=(0x[0-9a-f]+) max_error=([\d.]+) tolerance=([\d.]+)",
                           raw, re.MULTILINE)
        require(fixture is not None and region is not None, f"missing {method} fixture/region")
        require(float(fixture[1]) == float(region[5]) <= float(region[6]), f"{method} fixture failed")
        regions.append((int(region[2], 16), int(region[1]), int(region[4], 16), int(region[3])))
    weights = sorted((address, address + size) for address, size, _, _ in regions)
    require(all(right[0] >= left[1] for left, right in zip(weights, weights[1:])), "overlapping weight regions")
    require(all(0xC0000000 <= begin < end <= 0xC0000000 + model.stat().st_size for begin, end in weights),
            "weight region lies outside the measured PTE")
    require(len({region[2] for region in regions}) == 1, "arenas are not one shared allocation")
    pools = re.search(r"^POOLS method_peak=(\d+) temp_peak=(\d+)", raw, re.MULTILINE)
    require(pools is not None, "missing allocator observations")
    header = re.search(r"^PROFILE generation=(\d+) samples=(\d+) timer_hz=(\d+) OSPI_HZ=(\d+)", raw, re.MULTILINE)
    require(header is not None and int(header[2]) == 64 and int(header[4]) == int(clock[1]), "invalid profile header")
    vectors = []
    for name in ("PROFILE_CYCLES", "PROFILE_TICKS"):
        matches = re.findall(rf"^{name} ([\d ]+)$", raw, re.MULTILINE)
        require(len(matches) == 1, f"expected one {name} vector")
        values = list(map(int, matches[0].split()))
        require(len(values) == 64 and all(value > 0 for value in values), f"invalid {name} samples")
        vectors.append(values)
    cycles, ticks = vectors
    validation = json.loads((HERE / provenance["completed_sentence_validation"]).read_text(encoding="utf-8"))
    by_expression = {entry["expression"]: "\n".join(b["text"] for b in entry["result"]["content"]
                     if b.get("type") == "text") for entry in validation}
    for name, expected in (("pipeline_error", 0), ("handwriting_test_passed", 1), ("model_verified", 1)):
        require(re.search(rf"Result: {expected}\b", by_expression[name]) is not None, f"{name} validation failed")
    stats = {name: int(value) for name, value in re.findall(r"^  (\w+): (\d+) ", by_expression["stream_stats"], re.MULTILINE)}
    for name in ("error", "event_drops", "source_deadline_misses", "lcd_deadline_misses", "ink_overflows", "screen_full"):
        require(stats[name] == 0, f"nonzero {name}")
    require(stats["completed"] == 1 and stats["drawn"] == stats["generated"] == stats["sentence_points"],
            "sentence did not complete with every generated result processed")

    destination = HERE / "benchmark.yml"
    document = yaml.safe_load(destination.read_text(encoding="utf-8"))
    baseline = next(record for record in document["benchmarks"]["benchmark"]
                    if record["run"]["uart"] == "benchmark_uart.txt")
    record = deepcopy(baseline)
    record["platform"]["memory"]["weights"].update(clock_mhz=int(clock[1]) / 1e6,
        clock_source="projects/handwriting_synthesis/hardware_params.cpp APP_READ_CLOCK_HZ; guarded e8_flash_set_read_clock; observed AXI400MHz/divider4")
    record["measurements"]["memory"] = dict(model_bytes=int(model_size[1]), input_tensor_bytes=12,
        ethos=dict(weights_region_bytes=sum(end - begin for begin, end in weights),
                   arena_region_bytes=max(region[3] for region in regions)),
        executorch=dict(method_pool_peak_bytes=int(pools[1]), temp_pool_peak_bytes=int(pools[2])))
    timer_hz = int(header[3])
    npu_hz = provenance["npu_clock_hz"]
    record["measurements"]["time"] = dict(ms=statistics.median(cycles) * 1000 / npu_hz,
        cycles=statistics.median(cycles), statistic="median", min_cycles=min(cycles), max_cycles=max(cycles),
        mean_cycles=statistics.mean(cycles), forward_ms=statistics.median(ticks) * 1000 / timer_hz,
        forward_timer_hz=timer_hz, cycles_samples=cycles, forward_ticks_samples=ticks)
    record["run"].update(recorded_at=provenance["recorded_at"], build_job=provenance["build_job"],
        execution_job=provenance["execution_job"], uart="benchmark_100mhz_uart.txt",
        firmware_sha256=provenance["firmware_sha256"], model_sha256=provenance["model_sha256"],
        input="75 supported characters plus NUL, seed42, evolving sampled feedback; one warmup then64 measured steps; five startup held-out dense fixtures",
        uart_run="Autonomous Release demo with concurrent UART/LCD; no halt or debugger reads during the64 measured Engine steps. Completion snapshot taken after the full sentence.",
        measurement_source="Preserved CMSIS serial chunks benchmark_100mhz_demo_startup.json and benchmark_100mhz_demo_profile.json; per-step production PMU checks; demo_b35_75char_live.json; benchmark_100mhz_provenance.json. No per-method ten-sample timing at100MHz is claimed.")
    records = [old for old in document["benchmarks"]["benchmark"] if old["run"].get("uart") != record["run"]["uart"]]
    records.append(record)
    document["benchmarks"]["benchmark"] = records
    (HERE / record["run"]["uart"]).write_text(raw, encoding="utf-8")
    destination.write_text(yaml.safe_dump(document, sort_keys=False), encoding="utf-8")
    artifacts = {name: hashlib.sha256((HERE / name).read_bytes()).hexdigest()
                 for name in (provenance["startup_uart"], provenance["profile_uart"],
                              provenance["completed_sentence_validation"], provenance["linked_memory"])}
    result = dict(time=record["measurements"]["time"], artifact_sha256=artifacts,
        full_step_speedup=baseline["measurements"]["time"]["forward_ms"] / record["measurements"]["time"]["forward_ms"],
        processed_points_per_second=stats["sentence_points"] * 1000 / stats["sentence_elapsed_ms"],
        processed_points=stats["sentence_points"], point_count_scope="Generated results processed by the display, including pen-up/no-pixel results; not inked-point or frame count.",
        sentence_ms=stats["sentence_elapsed_ms"], lines=stats["lines"])
    (HERE / "benchmark_100mhz_summary.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: value for key, value in result.items() if key != "time"} | {
        "npu_ms": result["time"]["ms"], "forward_ms": result["time"]["forward_ms"]}, indent=2))


if __name__ == "__main__":
    main()
