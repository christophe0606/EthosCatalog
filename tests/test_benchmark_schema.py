# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Check portable benchmark structure, validation failures and Vela provenance."""
from copy import deepcopy
from contextlib import redirect_stderr, redirect_stdout
import csv
import hashlib
import io
import math
from pathlib import Path
import re
import sys
import tempfile
import unittest

import yaml

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from validate_benchmarks import benchmark_files, load_yaml, main, validation_errors, validator


class BenchmarkSchema(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        (ROOT / "tmp").mkdir(exist_ok=True)
        cls.check = validator()
        cls.paths = benchmark_files()
        cls.documents = [load_yaml(path) for path in cls.paths]
        cls.template = load_yaml(ROOT / "projects/template_mram/model/benchmark.yml")

    def errors(self, document):
        return list(validation_errors(document, self.check))

    def records(self):
        for path, document in zip(self.paths, self.documents):
            collection = document["benchmarks"]
            for index, record in enumerate(collection["benchmark"]):
                yield path, index, collection["network"], record

    def test_all_benchmarks_validate(self):
        self.assertGreaterEqual(len(self.paths), 2)
        for path, document in zip(self.paths, self.documents):
            with self.subTest(path=path):
                self.assertEqual(self.errors(document), [])

    def test_platform_is_independent_of_cmsis_and_vela(self):
        document = {"benchmarks": {
            "network": {"operation": "y = 2*x", "input_shape": [1, 512], "output_shape": [1, 512]},
            "benchmark": [{
                "platform": {"os": "Linux", "cpu": "Cortex-A78"},
                "measurements": {"time": {"ms": 1.25}},
                "run": {"recorded_at": "2026-10-15T12:00:00Z", "samples": 10, "test_result": "PASS"},
            }],
        }}
        self.assertEqual(self.errors(document), [])
        for key in ("os", "cpu"):
            invalid = deepcopy(document)
            del invalid["benchmarks"]["benchmark"][0]["platform"][key]
            self.assertTrue(self.errors(invalid))

    def test_multiple_configurations_share_network(self):
        document = deepcopy(self.template)
        records = document["benchmarks"]["benchmark"]
        records.append({
            "platform": {"os": "Linux", "cpu": "Cortex-A78"},
            "measurements": {"time": {"ms": 1.25}},
            "run": {"recorded_at": "2026-10-16T00:00:00Z", "samples": 20, "test_result": "PASS"},
        })
        self.assertEqual(self.errors(document), [])
        for key in ("platform", "measurements", "run"):
            invalid = deepcopy(document)
            del invalid["benchmarks"]["benchmark"][1][key]
            self.assertTrue(self.errors(invalid))
        records[1]["measurements"]["time"]["ms"] = -1
        errors = self.errors(document)
        self.assertTrue(any("$.benchmarks.benchmark[1].measurements.time.ms" in error for error in errors))

    def test_collection_requires_shared_network_and_nonempty_list(self):
        for key in ("network", "benchmark"):
            invalid = deepcopy(self.template)
            del invalid["benchmarks"][key]
            self.assertTrue(self.errors(invalid))
        for value in ([], {}, None, [None], [{}]):
            with self.subTest(value=value):
                invalid = deepcopy(self.template)
                invalid["benchmarks"]["benchmark"] = value
                self.assertTrue(self.errors(invalid))
        for key in ("operation", "input_shape", "output_shape"):
            invalid = deepcopy(self.template)
            del invalid["benchmarks"]["network"][key]
            self.assertTrue(self.errors(invalid))

    def test_network_must_be_shared_and_legacy_root_is_rejected(self):
        invalid = deepcopy(self.template)
        record = invalid["benchmarks"]["benchmark"][0]
        record["network"] = invalid["benchmarks"]["network"]
        self.assertTrue(self.errors(invalid))
        del invalid["benchmarks"]["network"]
        self.assertTrue(self.errors(invalid))
        self.assertTrue(self.errors({"benchmark": record}))

    def test_multiple_output_tensor_shapes_validate(self):
        document = deepcopy(self.template)
        network = document["benchmarks"]["network"]
        del network["output_shape"]
        network["output_shapes"] = [[1, 6, 6, 18], [1, 12, 12, 18]]
        self.assertEqual(self.errors(document), [])
        network["output_shape"] = [1, 3240]
        self.assertTrue(self.errors(document), "single and multiple shapes are exclusive")

    def test_invalid_multiple_output_shapes_rejected(self):
        for shapes in ([], [[1, 648]], [1, 648], [[1, 6, 6, 18], []],
                       [[1, 6, 6, 18], [1, 0, 12, 18]],
                       [[1, 6, 6, 18], [1, True, 12, 18]],
                       [[1, 6, 6, 18], [1, "12", 12, 18]]):
            with self.subTest(shapes=shapes):
                document = deepcopy(self.template)
                network = document["benchmarks"]["network"]
                del network["output_shape"]
                network["output_shapes"] = shapes
                self.assertTrue(self.errors(document))

    def test_unknown_keys_rejected_in_every_record_object(self):
        def objects(value, path=()):
            if isinstance(value, dict):
                yield path
                for key, child in value.items():
                    yield from objects(child, (*path, key))
            elif isinstance(value, list):
                for index, child in enumerate(value):
                    yield from objects(child, (*path, index))

        for document in self.documents:
            for path in objects(document):
                with self.subTest(path=path):
                    invalid = deepcopy(document)
                    target = invalid
                    for key in path:
                        target = target[key]
                    target["typo"] = 1
                    self.assertTrue(self.errors(invalid))

    def test_invalid_values_rejected(self):
        cases = [
            (("platform", "os"), " "),
            (("platform", "cpu"), 55),
            (("measurements", "time", "ms"), -1),
            (("measurements", "time", "cycles_samples"), []),
            (("measurements", "time", "ms"), float("nan")),
            (("measurements", "time", "ms"), float("inf")),
            (("measurements", "memory", "ethos", "arena_region_bytes"), True),
            (("measurements", "memory", "executorch", "method_pool_peak_bytes"), -1),
            (("run", "samples"), 0),
            (("run", "model_sha256"), "bad-hash"),
            (("run", "recorded_at"), "2026-02-30T00:00:00Z"),
            (("run", "recorded_at"), "2026-10-15T00:00:00"),
            (("network", "input_shape"), [1, 0]),
            (("estimates", "vela", "bandwidth", "sram", "average_bytes_per_second"), "0.93 GB/s"),
        ]
        for path, value in cases:
            with self.subTest(path=path, value=value):
                invalid = deepcopy(self.template)
                if path[0] == "network":
                    target = invalid["benchmarks"]
                else:
                    target = invalid["benchmarks"]["benchmark"][0]
                for key in path[:-1]:
                    target = target[key]
                target[path[-1]] = value
                self.assertTrue(self.errors(invalid))

    def test_incomplete_vela_estimates_rejected(self):
        invalid = deepcopy(self.template)
        del invalid["benchmarks"]["benchmark"][0]["estimates"]["vela"]["bandwidth"]
        self.assertTrue(self.errors(invalid))

    def test_application_and_linker_details_rejected(self):
        cases = [
            (("measurements",), "firmware", {"lcd_framebuffer_bytes": 1536000}),
            (("measurements",), "pools", {"reserved": 131072}),
            (("measurements", "memory"), "application_heap_reserved_bytes", 32768),
            (("measurements", "memory", "executorch"), "reserved", 131072),
            (("measurements", "memory", "ethos"), "section", "RW_SRAM0"),
            (("measurements", "memory", "ethos"), "address", 33619968),
            (("platform", "memory", "weights"), "section", "ER_ROM"),
            (("platform", "memory", "weights"), "address", 2149877152),
        ]
        for path, key, value in cases:
            with self.subTest(path=path, key=key):
                invalid = deepcopy(self.template)
                target = invalid["benchmarks"]["benchmark"][0]
                for part in path:
                    target = target[part]
                target[key] = value
                self.assertTrue(self.errors(invalid))

    def test_estimates_and_measurements_cannot_be_mixed(self):
        record = self.template["benchmarks"]["benchmark"][0]
        invalid = deepcopy(self.template)
        invalid["benchmarks"]["benchmark"][0]["measurements"]["vela"] = record["estimates"]["vela"]
        self.assertTrue(self.errors(invalid))
        invalid = deepcopy(self.template)
        invalid["benchmarks"]["benchmark"][0]["estimates"]["memory"] = record["measurements"]["memory"]
        self.assertTrue(self.errors(invalid))
        invalid = deepcopy(self.template)
        invalid["benchmarks"]["benchmark"][0]["vela"] = record["estimates"]["vela"]
        self.assertTrue(self.errors(invalid))

    def test_network_memory_matches_uart_observations(self):
        for path, index, network, record in self.records():
            if "uart" not in record["run"] or "memory" not in record["measurements"]:
                continue
            with self.subTest(path=path, index=index):
                uart_path = path.parent / record["run"]["uart"]
                # The schema also permits a transport description in this
                # text field. Only file references supply observations to parse.
                if not uart_path.suffix and not uart_path.is_file():
                    continue
                uart = uart_path.read_text(encoding="utf-8")
                observed = re.findall(r"Benchmark memory: model=(\d+) B, arena=(\d+) B at 0x[0-9a-fA-F]+, weights=(\d+) B", uart)
                if observed:
                    model, arena, weights = map(int, observed[-1])
                    pools = re.findall(r"Benchmark pools: method peak=(\d+) B, temp peak=(\d+) B", uart)
                else:
                    # Multi-method models report each region separately. Count
                    # the union of address ranges so a reused scratch arena is
                    # counted once, while disjoint method weights are summed.
                    regions = re.findall(
                        r"^REGION (\w+) weights=(\d+) weights_address=(0x[0-9a-fA-F]+) "
                        r"arena=(\d+) arena_address=(0x[0-9a-fA-F]+)", uart, re.MULTILINE)
                    self.assertTrue(regions, "UART has no recognized memory observations")
                    self.assertEqual(len({region[0] for region in regions}), len(regions),
                                     "UART contains repeated method observations")

                    def region_union_bytes(size_index, address_index):
                        intervals = sorted((int(region[address_index], 16), int(region[size_index]))
                                           for region in regions)
                        total, previous_end = 0, 0
                        for start, size in intervals:
                            end = start + size
                            total += max(0, end - max(start, previous_end))
                            previous_end = max(previous_end, end)
                        return total

                    weights = region_union_bytes(1, 2)
                    arena = region_union_bytes(3, 4)
                    models = re.findall(r"^Model=(\d+) bytes;", uart, re.MULTILINE)
                    self.assertEqual(len(models), 1, "UART must describe one complete run")
                    model = int(models[0])
                    pools = re.findall(r"^POOLS method_peak=(\d+) temp_peak=(\d+)", uart, re.MULTILINE)
                self.assertTrue(pools, "UART has no recognized pool observations")
                method, temp = map(int, pools[-1])
                memory = record["measurements"]["memory"]
                self.assertEqual(memory["model_bytes"], model)
                if network.get("float_boundary"):
                    self.assertEqual(memory["input_tensor_bytes"], math.prod(network["input_shape"]) * 4)
                self.assertEqual(memory["ethos"], {"weights_region_bytes": weights, "arena_region_bytes": arena})
                self.assertEqual(memory["executorch"], {"method_pool_peak_bytes": method, "temp_pool_peak_bytes": temp})

    def test_yaml_duplicates_and_non_string_keys_rejected(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "tmp") as directory:
            path = Path(directory) / "benchmark.yml"
            for content in ("benchmarks: {}\nbenchmarks: {}\n", "benchmarks:\n  1: value\n",
                            "benchmarks:\n  benchmark:\n  - run: {}\n    run: {}\n"):
                path.write_text(content, encoding="utf-8")
                with self.assertRaises(ValueError):
                    load_yaml(path)

    def test_cli_returns_failure_for_invalid_or_missing_file(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "tmp") as directory, redirect_stderr(io.StringIO()):
            path = Path(directory) / "benchmark.yml"
            path.write_text("benchmarks: {}\n", encoding="utf-8")
            self.assertEqual(main([str(path)]), 1)
            self.assertEqual(main([str(path.with_name("missing.yml"))]), 1)
            path.write_text("benchmarks: [\n", encoding="utf-8")
            self.assertEqual(main([str(path)]), 1)

    def test_cli_validates_every_configuration(self):
        document = deepcopy(self.template)
        records = document["benchmarks"]["benchmark"]
        records.append(deepcopy(records[0]))
        records[1]["run"]["cpu_clock_mhz"] = 200
        records[1]["measurements"]["time"]["ms"] = 0.0025
        with tempfile.TemporaryDirectory(dir=ROOT / "tmp") as directory:
            path = Path(directory) / "benchmarks.yml"
            path.write_text(yaml.safe_dump(document), encoding="utf-8")
            stdout = io.StringIO()
            with redirect_stdout(stdout):
                self.assertEqual(main([str(path)]), 0)
            self.assertIn(f"OK {path}", stdout.getvalue())
            records[1]["run"]["samples"] = 0
            path.write_text(yaml.safe_dump(document), encoding="utf-8")
            stderr = io.StringIO()
            with redirect_stderr(stderr):
                self.assertEqual(main([str(path)]), 1)
            self.assertIn("$.benchmarks.benchmark[1].run.samples", stderr.getvalue())

    def test_bandwidth_matches_raw_vela_summary(self):
        for path, index, _, record in self.records():
            if "vela" not in record.get("estimates", {}):
                continue
            with self.subTest(path=path, index=index):
                vela = record["estimates"]["vela"]
                if "model_sha256" in record["run"]:
                    self.assertEqual(vela["model_sha256"], record["run"]["model_sha256"])
                with (path.parent / vela["summary"]).open(newline="") as stream:
                    rows = list(csv.DictReader(stream))
                self.assertEqual(len(rows), 1)
                row = rows[0]
                ini = (path.parent.parent / vela["ini"]).read_bytes()
                self.assertEqual(hashlib.sha256(ini).hexdigest(), vela["ini_sha256"])
                self.assertEqual(row["system_config"], vela["system_config"])
                self.assertEqual(row["memory_mode"], vela["memory_mode"])
                self.assertEqual(row["accelerator_configuration"], vela["accelerator_config"])
                self.assertEqual(vela["cycles"], int(row["cycles_total"]))
                self.assertAlmostEqual(vela["time_ms"], float(row["inference_time"]) * 1000)
                for memory in vela["bandwidth"].values():
                    area = memory["vela_memory_area"]
                    total = float(row[f"{area}_total_bytes"])
                    self.assertEqual(memory["total_bytes_per_inference"], total / int(row["batch_size"]))
                    self.assertAlmostEqual(memory["average_bytes_per_second"], total / float(row["inference_time"]), delta=0.001)
                    self.assertAlmostEqual(memory["design_peak_bytes_per_second"], float(row[f"{area}_bandwidth"]) * 2**30, delta=0.001)


if __name__ == "__main__":
    (ROOT / "tmp").mkdir(exist_ok=True)
    unittest.main()
