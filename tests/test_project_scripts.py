# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Behavior checks for project selection, quantized cache reuse and model storage."""
from copy import deepcopy
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import yaml

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from create_ai_layer import c_array, header, prepare_quantized_model
from quantized_model import cache_provenance, compile_spec, load_quantized_program, model_assets, sha256
from project_config import selected_project
import create_ai_layer


class ProjectScripts(unittest.TestCase):
    def test_application_cprojects_reference_their_own_layers(self):
        solution = yaml.safe_load((ROOT / "EthosCatalog.csolution.yml").read_text())["solution"]
        for item in solution["projects"]:
            project_file = ROOT / item["project"]
            if not (project_file.parent / "model/network.yml").is_file():
                continue
            project = yaml.safe_load(project_file.read_text())["project"]
            layers = [item["layer"] for item in project["layers"] if item.get("type") == "AI"]
            self.assertEqual(layers, ["ai_layer/ai_layer.clayer.yml"], str(project_file))

    def setUp(self):
        (ROOT / "tmp").mkdir(exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix="project-tests-", dir=ROOT / "tmp")
        self.root = Path(self.temp.name)
        self.project = self.root / "projects/another"
        self.project.mkdir(parents=True)
        (self.project / "model").mkdir()
        (self.project / "another.cproject.yml").write_text("project: {}\n")
        self.solution = deepcopy(yaml.safe_load((ROOT / "EthosCatalog.csolution.yml").read_text())["solution"])
        self.solution["projects"] = [{"project": "projects/another/another.cproject.yml"}]
        self.network = deepcopy(yaml.safe_load((ROOT / "projects/template_mram/model/network.yml").read_text())["network"])
        self.network["name"] = "AnotherNetwork"
        self.network["vela"]["ini"] = str(ROOT / ".cmsis/ensemble_vela.ini")
        self.solution_file = self.root / "Selected.csolution.yml"
        self.config_file = self.project / "model/network.yml"
        self.write_config()

    def tearDown(self):
        assert self.root.resolve().is_relative_to((ROOT / "tmp").resolve())
        self.temp.cleanup()

    def write_config(self):
        self.solution_file.write_text(yaml.safe_dump({"solution": self.solution}))
        self.config_file.write_text(yaml.safe_dump({"network": self.network}))

    def add_project(self, name, application=True):
        project = self.root / f"projects/{name}/{name}.cproject.yml"
        project.parent.mkdir(parents=True)
        project.write_text("project: {}\n")
        if application:
            (project.parent / "model").mkdir()
            network = deepcopy(self.network)
            network["name"] = name
            (project.parent / "model/network.yml").write_text(yaml.safe_dump({"network": network}))
        self.solution["projects"].append({"project": project.relative_to(self.root).as_posix()})
        self.write_config()
        return project

    def select_contexts(self, *contexts):
        self.solution["target-types"] = [{"type": "DevKit-E8", "target-set": [
            {"set": "Other", "images": [{"project-context": "unselected.Debug"}]},
            {"set": "Selected", "images": [{"project-context": c} for c in contexts]},
        ]}]
        self.write_config()
        settings = self.root / ".vscode/cmsis.json"
        settings.parent.mkdir(exist_ok=True)
        settings.write_text(json.dumps({
            "activeSolution": "../Selected.csolution.yml",
            # Informational activeTarget may lag; targetSet is authoritative.
            "activeTarget": "DevKit-E8@Other",
            "targetSet": {"Selected": {"activeTargetType": "DevKit-E8", "DevKit-E8": 1}},
        }))

    def flash_script(self, *args):
        return subprocess.run([sys.executable, str(ROOT / "scripts/create_model_flash.py"),
                               "--solution", str(self.solution_file), *map(str, args)],
                              text=True, capture_output=True)

    def prepare_ospi(self):
        self.network["flash_ospi"] = True
        self.network["ospi"] = {"address": 0xC0010000, "size": 0x10000}
        self.write_config()
        layer = self.project / "ai_layer"
        layer.mkdir()
        pte = layer / "model.pte"
        pte.write_bytes(b"fixture network payload")
        cache = self.project / self.network["safetensors"]
        cache.parent.mkdir(exist_ok=True)
        cache.write_bytes(b"fixture cache")
        (self.project / self.network["graph"]).write_bytes(b"fixture graph")
        report = {"project": "projects/another/another.cproject.yml", "config_sha256": sha256(self.config_file),
                  "pte_sha256": sha256(pte), **cache_provenance(self.project, self.network), "flash_ospi": True}
        (layer / "generation.json").write_text(json.dumps(report))
        runs = self.root / "application"
        runs.mkdir()
        (runs / "algorithm.FLM").write_bytes(b"fixture algorithm")
        self.run_file = runs / "application.cbuild-run.yml"
        self.run_file.write_text(yaml.safe_dump({"cbuild-run": {
            "output": [{"file": "application.axf", "type": "elf", "load": "image"}],
            "programming": [{"algorithm": "algorithm.FLM", "start": 0xC0000000, "size": 0x02000000}],
        }}))
        self.output = self.root / "result/model.cbuild-run.yml"

    def test_multiple_projects_require_selection(self):
        self.add_project("second")
        with self.assertRaisesRegex(ValueError, "exactly one"):
            selected_project(self.solution_file)

    def test_selection_requires_local_model_and_quantizer(self):
        self.network["model"] = "../../outside.py"
        self.write_config()
        with self.assertRaisesRegex(ValueError, "inside its project"):
            selected_project(self.solution_file)
        self.network["model"] = "model/model.py"
        self.network["quantize"] = "../../outside.py"
        self.write_config()
        with self.assertRaisesRegex(ValueError, "inside its project"):
            selected_project(self.solution_file)

    def test_selection_reads_and_validates_quantization_metadata(self):
        for provenance in ("full", "partial", "already", "synthetic"):
            with self.subTest(provenance=provenance):
                self.network["quantization"].update(quantization=provenance, error={
                    "recognition_percent": 96.1, "signed_delta": -0.4,
                    "confusion_matrix": [[92, 8], [5, 95]],
                    "future_metric": {"units": "custom", "values": [1, 2]},
                })
                self.write_config()
                self.assertEqual(selected_project(self.solution_file)[4]["quantization"],
                                 self.network["quantization"])
        for invalid in (None, {"type": "post_training_quantization"},
                        {"type": "post_training_quantization", "quantization": "fake", "error": None},
                        {"type": "unknown", "quantization": "synthetic", "error": {}}):
            with self.subTest(invalid=invalid):
                self.network["quantization"] = invalid
                self.write_config()
                with self.assertRaisesRegex(ValueError, r"network\.quantization"):
                    selected_project(self.solution_file)
        del self.network["quantization"]
        self.write_config()
        with self.assertRaisesRegex(ValueError, "quantization.*required"):
            selected_project(self.solution_file)

    def test_ui_selects_application_among_multiple_listed_projects(self):
        second = self.add_project("second")
        self.add_project("FlashLoader", application=False)
        self.select_contexts("second.Release", "FlashLoader.FlashLoader")
        # A stale generated build-set must not override the current UI choice.
        (self.root / "Selected.cbuild-set.yml").write_text(yaml.safe_dump({
            "cbuild-set": {"contexts": [{"context": "another.Debug+DevKit-E8"}]}}))
        result = selected_project(self.solution_file)
        self.assertEqual(result[2], second)
        self.assertEqual(result[4]["name"], "second")
        self.select_contexts("another.Debug")
        self.assertEqual(selected_project(self.solution_file)[2].parent, self.project)

    def test_unnamed_target_set_and_nested_solution_use_workspace_selection(self):
        self.add_project("second")
        self.select_contexts("another.Debug")
        self.solution["target-types"][0]["target-set"][1]["set"] = None
        nested = self.root / "nested"
        nested.mkdir()
        self.solution_file = nested / self.solution_file.name
        for entry in self.solution["projects"]:
            entry["project"] = "../" + entry["project"]
        self.write_config()
        settings = self.root / ".vscode/cmsis.json"
        settings.write_text(json.dumps({"targetSet": {
            "nested/Selected": {"activeTargetType": "DevKit-E8"}}}))
        self.assertEqual(selected_project(self.solution_file)[2].parent, self.project)

    def test_legacy_build_set_selects_project_without_ui_settings(self):
        second = self.add_project("second")
        (self.root / "Selected.cbuild-set.yml").write_text(yaml.safe_dump({
            "cbuild-set": {"contexts": [{"context": "second.Release+DevKit-E8"}]}}))
        self.assertEqual(selected_project(self.solution_file)[2], second)

    def test_ambiguous_or_empty_ui_selection_is_rejected(self):
        self.add_project("second")
        self.add_project("FlashLoader", application=False)
        for contexts in (("another.Debug", "second.Debug"), ("FlashLoader.FlashLoader",), ()):
            with self.subTest(contexts=contexts):
                self.select_contexts(*contexts)
                with self.assertRaisesRegex(ValueError, "exactly one application"):
                    selected_project(self.solution_file)

    def test_stale_ui_selection_is_rejected(self):
        self.select_contexts("removed.Debug")
        with self.assertRaisesRegex(ValueError, "not listed"):
            selected_project(self.solution_file)
        self.solution["target-types"] = []
        self.write_config()
        with self.assertRaisesRegex(ValueError, "selected target"):
            selected_project(self.solution_file)

    def test_explicit_project_overrides_ui_by_name_or_path(self):
        second = self.add_project("second")
        self.select_contexts("another.Debug")
        for selector in ("second", "projects/second/second.cproject.yml", second):
            with self.subTest(selector=selector):
                self.assertEqual(selected_project(self.solution_file, selector)[2], second)
        with self.assertRaisesRegex(ValueError, "--project"):
            selected_project(self.solution_file, "missing")

    def test_flash_cli_uses_ui_or_explicit_project(self):
        self.add_project("second")
        self.select_contexts("second.Release")
        result = self.flash_script()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("second: flash_ospi is false", result.stdout)
        result = self.flash_script("--project", "another")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("AnotherNetwork: flash_ospi is false", result.stdout)

    def test_ai_generation_uses_selected_project_with_multiple_listed(self):
        second = self.add_project("second")
        original_layer = self.project / "ai_layer"
        original_layer.mkdir()
        (original_layer / "model.pte").write_bytes(b"first project PTE")
        self.select_contexts("second.Release")
        (second.parent / self.network["safetensors"]).write_bytes(b"second cache")
        (second.parent / self.network["graph"]).write_bytes(b"second graph")
        with (patch.object(sys, "argv", ["create_ai_layer.py", "--solution", str(self.solution_file)]),
              patch.object(create_ai_layer, "executorch_pack", return_value=("1.4.1", self.root)),
              patch.object(create_ai_layer, "prepare_quantized_model"),
              patch.object(create_ai_layer, "compile_spec"),
              patch.object(create_ai_layer, "load_quantized_program"),
              patch.object(create_ai_layer, "export_model", return_value=(b"second PTE", 1)),
              patch.object(create_ai_layer, "components", return_value=(["Runtime"], []))):
            create_ai_layer.main()
        layer = second.parent / "ai_layer"
        self.assertFalse((self.root / "ai_layer").exists())
        self.assertEqual((layer / "model.pte").read_bytes(), b"second PTE")
        report = json.loads((layer / "generation.json").read_text())
        self.assertEqual(report["project"], "projects/second/second.cproject.yml")
        self.assertEqual(yaml.safe_load((layer / "ai_layer.clayer.yml").read_text())["layer"]["description"], "second")
        self.assertFalse((self.project / self.network["safetensors"]).exists())
        self.assertEqual((original_layer / "model.pte").read_bytes(), b"first project PTE")

    def test_mram_skips_programming_and_embeds_model(self):
        result = self.flash_script("--output", self.root / "unused.yml")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("flash_ospi is false", result.stdout)
        self.assertFalse((self.root / "unused.yml").exists())
        self.assertIn("0x01, 0x02", c_array(b"\x01\x02", self.network))
        self.assertNotIn("0xc0", c_array(b"\x01\x02", self.network))

    def test_model_header_supplies_exact_length_and_crc_for_ospi_readback(self):
        # Standard CRC-32/ISO-HDLC check vector, also used by the firmware.
        self.network["flash_ospi"] = True
        generated = header(self.network, b"123456789")
        self.assertIn("#define MODEL_PTE_SIZE_BYTES 9U", generated)
        self.assertIn("#define MODEL_PTE_CRC32 0xcbf43926U", generated)
        self.assertIn("extern const unsigned char *const model_pte;", generated)

    def test_ospi_uses_selected_model_address_and_preserves_relative_algorithm(self):
        self.prepare_ospi()
        # A legacy global layer must never be selected, even if it exists.
        legacy = self.root / "ai_layer"
        legacy.mkdir()
        (legacy / "model.pte").write_bytes(b"wrong global model")
        self.add_project("unselected")
        self.select_contexts("another.Release")
        result = self.flash_script("--run", self.run_file, "--output", self.output)
        self.assertEqual(result.returncode, 0, result.stderr)
        run = yaml.safe_load(self.output.read_text())["cbuild-run"]
        self.assertEqual(len(run["output"]), 1)
        self.assertEqual(run["output"][0]["file"], (self.project / "ai_layer/model.pte").as_posix())
        self.assertEqual(run["output"][0]["load-offset"], 0xC0010000)
        self.assertIn("AnotherNetwork", run["output"][0]["info"])
        self.assertEqual(run["programming"][0]["algorithm"], (self.run_file.parent / "algorithm.FLM").as_posix())
        source = c_array(b"\x01\x02", self.network)
        self.assertIn("0xc0010000", source)
        self.assertNotIn("0x01, 0x02", source)

    def test_ospi_rejects_stale_model_and_too_small_partition(self):
        self.prepare_ospi()
        (self.project / "ai_layer/model.pte").write_bytes(b"changed payload")
        result = self.flash_script("--run", self.run_file, "--output", self.output)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("does not match", result.stderr)
        self.assertFalse(self.output.exists())
        self.network["ospi"]["size"] = 1
        with self.assertRaisesRegex(ValueError, "does not fit"):
            c_array(b"\x01\x02", self.network)

    def test_ospi_explicit_algorithm_selects_matching_overlapping_range(self):
        self.prepare_ospi()
        loader = self.run_file.parent / "project.FLM"
        loader.write_bytes(b"project loader")
        run = yaml.safe_load(self.run_file.read_text())
        selected = {"algorithm": "project.FLM", "start": 0xC0000000,
                    "size": 0x04000000, "ram-start": 0, "ram-size": 0x40000}
        run["cbuild-run"]["programming"].append(selected)
        self.run_file.write_text(yaml.safe_dump(run))
        result = self.flash_script("--run", self.run_file, "--output", self.output)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Expected one programming algorithm", result.stderr)
        result = self.flash_script("--run", self.run_file, "--output", self.output,
                                   "--algorithm", loader)
        self.assertEqual(result.returncode, 0, result.stderr)
        programmed = yaml.safe_load(self.output.read_text())["cbuild-run"]
        self.assertEqual(programmed["programming"], [{**selected, "algorithm": loader.as_posix()}])

    def test_ospi_assets_are_bounded_hashed_and_programmed(self):
        self.prepare_ospi()
        base = self.network["ospi"]["address"]
        self.network["assets"] = {
            "prefix": {"file": "model/prefix.bin", "address": base + 0x1000},
            "bos": {"file": "model/bos.bin", "address": base + 0x2000},
        }
        for asset in self.network["assets"].values():
            (self.project / asset["file"]).write_bytes(b"123456789")
        self.write_config()
        assets = model_assets(self.project, self.network, 23)
        generated = header(self.network, b"pte", assets)
        self.assertIn("#define MODEL_ASSET_PREFIX_SIZE_BYTES 9U", generated)
        self.assertIn("#define MODEL_ASSET_PREFIX_CRC32 0xcbf43926U", generated)
        report_path = self.project / "ai_layer/generation.json"
        report = json.loads(report_path.read_text())
        report.update(config_sha256=sha256(self.config_file), assets=assets)
        report_path.write_text(json.dumps(report))
        result = self.flash_script("--run", self.run_file, "--output", self.output)
        self.assertEqual(result.returncode, 0, result.stderr)
        run = yaml.safe_load(self.output.read_text())["cbuild-run"]
        self.assertEqual(run["output"][0]["load-offset"], base)
        self.assertEqual({item["load-offset"] for item in run["output"][1:]},
                         {base + 0x1000, base + 0x2000})
        (self.project / "model/bos.bin").write_bytes(b"changed")
        result = self.flash_script("--run", self.run_file, "--output", self.output)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("does not match", result.stderr)

    def test_ospi_rejects_changed_single_graph_with_unchanged_weights(self):
        self.prepare_ospi()
        (self.project / self.network["graph"]).write_bytes(b"changed operator graph")
        result = self.flash_script("--run", self.run_file, "--output", self.output)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("does not match", result.stderr)
        self.assertFalse(self.output.exists())

    def test_ospi_assets_reject_overlap_out_of_bounds_and_external_paths(self):
        self.prepare_ospi()
        base = self.network["ospi"]["address"]
        (self.project / "model/asset.bin").write_bytes(b"asset")
        self.network["assets"] = {"prefix": {"file": "model/asset.bin", "address": base}}
        with self.assertRaisesRegex(ValueError, "overlaps PTE"):
            model_assets(self.project, self.network, 23)
        self.network["assets"]["prefix"]["address"] = base + self.network["ospi"]["size"] - 1
        with self.assertRaisesRegex(ValueError, "exceeds"):
            model_assets(self.project, self.network, 23)
        self.network["assets"]["prefix"]["address"] = base + 0x1000
        self.network["assets"]["bos"] = dict(self.network["assets"]["prefix"])
        with self.assertRaisesRegex(ValueError, "overlaps prefix"):
            model_assets(self.project, self.network, 23)
        del self.network["assets"]["bos"]
        self.network["assets"]["prefix"]["file"] = "../../outside.bin"
        with self.assertRaisesRegex(ValueError, "inside model"):
            model_assets(self.project, self.network, 23)

    def test_named_methods_validate_paths_and_bind_all_flash_caches(self):
        self.prepare_ospi()
        del self.network["graph"]
        del self.network["safetensors"]
        self.network["methods"] = {
            name: {"graph": f"model/{name}.pt2", "safetensors": f"model/{name}.safetensors",
                   "delegate": name != "text_embedding"}
            for name in ("backbone", "synthesize", "text_embedding")
        }
        for name, method in self.network["methods"].items():
            for key in ("graph", "safetensors"):
                (self.project / method[key]).write_bytes(f"{name} {key}".encode())
        self.write_config()
        self.assertEqual(selected_project(self.solution_file)[4]["methods"], self.network["methods"])
        report_path = self.project / "ai_layer/generation.json"
        report = json.loads(report_path.read_text())
        report.pop("safetensors_sha256")
        report.update(cache_provenance(self.project, self.network))
        report["config_sha256"] = sha256(self.config_file)
        report_path.write_text(json.dumps(report))
        result = self.flash_script("--run", self.run_file, "--output", self.output)
        self.assertEqual(result.returncode, 0, result.stderr)
        (self.project / self.network["methods"]["text_embedding"]["safetensors"]).write_bytes(b"changed CPU lookup")
        result = self.flash_script("--run", self.run_file, "--output", self.output)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("does not match", result.stderr)
        self.network["methods"]["text_embedding"]["graph"] = "../../outside.pt2"
        self.write_config()
        with self.assertRaisesRegex(ValueError, "inside its project"):
            selected_project(self.solution_file)

    def test_entry_point_calls_selected_quantizer_on_first_run_and_force(self):
        script = self.project / self.network["quantize"]
        script.write_text('''import argparse
from pathlib import Path
parser = argparse.ArgumentParser()
parser.add_argument("--solution", required=True)
parser.add_argument("--project", required=True)
args = parser.parse_args()
project = Path(args.project).resolve().parent
marker = project / "calls.txt"
with marker.open("a") as calls:
    calls.write(str(Path(args.solution).resolve()) + "|" + str(Path(args.project).resolve()) + "\\n")
cache = project / "model/model_quantized.safetensors"
cache.parent.mkdir(exist_ok=True)
cache.write_bytes(b"project-owned quantization")
''')
        marker = self.project / "calls.txt"
        project_file = self.project / "another.cproject.yml"
        # The child must receive the resolved project, even if UI selection changes.
        self.add_project("second")
        self.select_contexts("second.Release")
        prepare_quantized_model(self.solution_file, project_file, self.network)
        self.assertEqual(marker.read_text().splitlines(), [f"{self.solution_file}|{project_file}"])
        prepare_quantized_model(self.solution_file, project_file, self.network)
        self.assertEqual(len(marker.read_text().splitlines()), 1)
        prepare_quantized_model(self.solution_file, project_file, self.network, force=True)
        self.assertEqual(len(marker.read_text().splitlines()), 2)

    def test_project_quantization_and_cache_reuse_leave_test_data_untouched(self):
        import torch
        from safetensors.torch import load_file
        script = ROOT / "projects/template_mram/model/quantize.py"
        module_spec = importlib.util.spec_from_file_location("template_quantizer", script)
        quantizer = importlib.util.module_from_spec(module_spec)
        module_spec.loader.exec_module(quantizer)
        (self.project / self.network["quantize"]).write_bytes(script.read_bytes())
        test_header = self.project / "model/model_test_data.h"
        fixed_header = (ROOT / "projects/template_mram/model/model_test_data.h").read_bytes()
        test_header.write_bytes(fixed_header)
        model_dir = self.project / "model"
        model_dir.mkdir(exist_ok=True)
        source = model_dir / "model.py"
        original = (ROOT / "projects/template_mram/model/model.py").read_text()
        source.write_text(original)
        spec = compile_spec(self.solution, self.project, self.network)
        quantizer.quantize_model(spec, self.project, self.network)
        program = load_quantized_program(spec.target, self.project, self.network)
        sample = quantizer.calibration_inputs()[0]
        with torch.no_grad():
            expected = program.module()(sample)
        weights = self.project / self.network["safetensors"]
        first_hash = sha256(weights)
        tensors = {key: value.clone() for key, value in load_file(weights).items()}
        self.assertTrue(any(t.dtype == torch.int16 for t in tensors.values()))
        self.assertFalse(any(t.dtype == torch.int8 for t in tensors.values()))
        quantizers = [node for node in program.graph.nodes
                      if node.target == torch.ops.quantized_decomposed.quantize_per_tensor.default]
        self.assertTrue(quantizers)
        self.assertTrue(all(node.args[-1] == torch.int16 for node in quantizers))
        self.assertTrue(any(node.target == torch.ops.aten.mul.Tensor for node in program.graph.nodes))
        self.assertFalse(any('conv' in str(node.target) for node in program.graph.nodes))
        torch.testing.assert_close(expected, 2.0*sample, atol=0.0001, rtol=0)
        self.assertFalse(any(key.startswith("test.") for key in tensors))
        source.write_text("raise RuntimeError('original network was imported')\n")
        program = load_quantized_program(spec.target, self.project, self.network)
        with torch.no_grad():
            torch.testing.assert_close(program.module()(sample), expected)
        self.assertEqual(sha256(weights), first_hash)
        with self.assertRaisesRegex(RuntimeError, "original network was imported"):
            quantizer.quantize_model(spec, self.project, self.network)
        source.write_text(original.replace("GAIN = 2.0", "GAIN = 1.5"))
        quantizer.quantize_model(spec, self.project, self.network)
        regenerated = load_quantized_program(spec.target, self.project, self.network)
        with torch.no_grad():
            self.assertFalse(torch.equal(regenerated.module()(sample), expected))
        self.assertNotEqual(sha256(weights), first_hash)
        self.assertEqual(test_header.read_bytes(), fixed_header)


if __name__ == "__main__":
    unittest.main()
