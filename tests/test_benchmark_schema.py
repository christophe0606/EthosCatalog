"""Check portable benchmark structure, validation failures and Vela provenance."""
from copy import deepcopy
from contextlib import redirect_stderr
import csv
import hashlib
import io
import math
from pathlib import Path
import re
import sys
import tempfile
import unittest

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

    def test_all_benchmarks_validate(self):
        self.assertGreaterEqual(len(self.paths), 2)
        for path, document in zip(self.paths, self.documents):
            with self.subTest(path=path):
                self.assertEqual(self.errors(document), [])

    def test_platform_is_independent_of_cmsis_and_vela(self):
        document = {"benchmark": {
            "platform": {"os": "Linux", "cpu": "Cortex-A78"},
            "measurements": {"time": {"ms": 1.25}},
            "run": {"recorded_at": "2026-10-15T12:00:00Z", "samples": 10, "test_result": "PASS"},
            "network": {"operation": "y = 2*x", "input_shape": [1, 512], "output_shape": [1, 512]},
        }}
        self.assertEqual(self.errors(document), [])
        for key in ("os", "cpu"):
            invalid = deepcopy(document)
            del invalid["benchmark"]["platform"][key]
            self.assertTrue(self.errors(invalid))

    def test_unknown_keys_rejected_in_every_record_object(self):
        def objects(value, path=()):
            if isinstance(value, dict):
                yield path
                for key, child in value.items():
                    yield from objects(child, (*path, key))

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
                invalid = deepcopy(self.template)["benchmark"]
                target = invalid
                for key in path[:-1]:
                    target = target[key]
                target[path[-1]] = value
                self.assertTrue(self.errors({"benchmark": invalid}))

    def test_incomplete_vela_estimates_rejected(self):
        invalid = deepcopy(self.template)
        del invalid["benchmark"]["estimates"]["vela"]["bandwidth"]
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
                target = invalid["benchmark"]
                for part in path:
                    target = target[part]
                target[key] = value
                self.assertTrue(self.errors(invalid))

    def test_estimates_and_measurements_cannot_be_mixed(self):
        record = self.template["benchmark"]
        invalid = deepcopy(self.template)
        invalid["benchmark"]["measurements"]["vela"] = record["estimates"]["vela"]
        self.assertTrue(self.errors(invalid))
        invalid = deepcopy(self.template)
        invalid["benchmark"]["estimates"]["memory"] = record["measurements"]["memory"]
        self.assertTrue(self.errors(invalid))
        invalid = deepcopy(self.template)
        invalid["benchmark"]["vela"] = record["estimates"]["vela"]
        self.assertTrue(self.errors(invalid))

    def test_network_memory_matches_uart_observations(self):
        for path, document in zip(self.paths, self.documents):
            record = document["benchmark"]
            if "uart" not in record["run"] or "memory" not in record["measurements"]:
                continue
            with self.subTest(path=path):
                uart = (path.parent / record["run"]["uart"]).read_text()
                observed = re.findall(r"Benchmark memory: model=(\d+) B, arena=(\d+) B at 0x[0-9a-fA-F]+, weights=(\d+) B", uart)
                model, arena, weights = map(int, observed[-1])
                method, temp = map(int, re.findall(r"Benchmark pools: method peak=(\d+) B, temp peak=(\d+) B", uart)[-1])
                memory = record["measurements"]["memory"]
                self.assertEqual(memory["model_bytes"], model)
                if record["network"].get("float_boundary"):
                    self.assertEqual(memory["input_tensor_bytes"], math.prod(record["network"]["input_shape"]) * 4)
                self.assertEqual(memory["ethos"], {"weights_region_bytes": weights, "arena_region_bytes": arena})
                self.assertEqual(memory["executorch"], {"method_pool_peak_bytes": method, "temp_pool_peak_bytes": temp})

    def test_yaml_duplicates_and_non_string_keys_rejected(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "tmp") as directory:
            path = Path(directory) / "benchmark.yml"
            for content in ("benchmark: {}\nbenchmark: {}\n", "benchmark:\n  1: value\n"):
                path.write_text(content, encoding="utf-8")
                with self.assertRaises(ValueError):
                    load_yaml(path)

    def test_cli_returns_failure_for_invalid_or_missing_file(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "tmp") as directory, redirect_stderr(io.StringIO()):
            path = Path(directory) / "benchmark.yml"
            path.write_text("benchmark: {}\n", encoding="utf-8")
            self.assertEqual(main([str(path)]), 1)
            self.assertEqual(main([str(path.with_name("missing.yml"))]), 1)
            path.write_text("benchmark: [\n", encoding="utf-8")
            self.assertEqual(main([str(path)]), 1)

    def test_bandwidth_matches_raw_vela_summary(self):
        for path, document in zip(self.paths, self.documents):
            record = document["benchmark"]
            if "vela" not in record.get("estimates", {}):
                continue
            with self.subTest(path=path):
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
