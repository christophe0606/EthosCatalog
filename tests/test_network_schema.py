"""Check network configuration structure, OSPI conditions and validator CLI."""
from contextlib import redirect_stderr, redirect_stdout
from copy import deepcopy
import io
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import yaml

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from validate_networks import load_yaml, main, network_files, validation_errors, validator


class NetworkSchema(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        (ROOT / "tmp").mkdir(exist_ok=True)
        cls.check = validator()
        cls.paths = network_files()
        cls.documents = [load_yaml(path) for path in cls.paths]
        cls.template = load_yaml(ROOT / "projects/template_mram/model/network.yml")
        cls.ospi = load_yaml(ROOT / "projects/template_ospi/model/network.yml")

    def errors(self, document):
        return list(validation_errors(document, self.check))

    def test_existing_networks_validate(self):
        self.assertGreaterEqual(len(self.paths), 3)
        for path, document in zip(self.paths, self.documents):
            with self.subTest(path=path):
                self.assertEqual(self.errors(document), [])

    def test_required_fields(self):
        records = [
            ((), ("network",)),
            (("network",), ("name", "model", "quantize", "safetensors", "graph", "flash_ospi", "vela")),
            (("network", "vela"), ("ini", "system_config", "memory_mode")),
            (("network", "ospi"), ("address", "size")),
        ]
        for path, keys in records:
            for key in keys:
                with self.subTest(path=path, key=key):
                    invalid = deepcopy(self.ospi)
                    record = invalid
                    for part in path:
                        record = record[part]
                    del record[key]
                    self.assertTrue(self.errors(invalid))

    def test_unknown_keys_rejected_at_every_level(self):
        for path in ((), ("network",), ("network", "vela"), ("network", "ospi")):
            with self.subTest(path=path):
                invalid = deepcopy(self.ospi)
                record = invalid
                for part in path:
                    record = record[part]
                record["typo"] = "value"
                self.assertTrue(self.errors(invalid))

    def test_text_fields_reject_empty_and_non_string_values(self):
        paths = [("name",), ("model",), ("quantize",), ("safetensors",), ("graph",),
                 ("vela", "ini"), ("vela", "system_config"), ("vela", "memory_mode"),
                 ("ospi", "pname")]
        for path in paths:
            for value in ("", " \t\n", None, 123, False, [], {}):
                with self.subTest(path=path, value=value):
                    invalid = deepcopy(self.ospi)
                    record = invalid["network"]
                    for part in path[:-1]:
                        record = record[part]
                    record[path[-1]] = value
                    self.assertTrue(self.errors(invalid))

    def test_ospi_required_only_when_enabled(self):
        invalid = deepcopy(self.template)
        invalid["network"]["flash_ospi"] = True
        self.assertTrue(self.errors(invalid))
        for value in ("true", "false", 0, 1, None):
            with self.subTest(value=value):
                invalid["network"]["flash_ospi"] = value
                self.assertTrue(self.errors(invalid))
        valid = deepcopy(self.ospi)
        del valid["network"]["ospi"]["pname"]
        self.assertEqual(self.errors(valid), [])
        valid["network"]["flash_ospi"] = False
        self.assertEqual(self.errors(valid), [])

    def test_ospi_partition_numeric_constraints(self):
        for field, values in {
            "address": (-1, "0xC0000000", True, None, 1.5, float("inf"), float("nan")),
            "size": (0, -1, "33554432", False, None, 1.5, float("inf"), float("nan")),
        }.items():
            for value in values:
                with self.subTest(field=field, value=value):
                    invalid = deepcopy(self.ospi)
                    invalid["network"]["ospi"][field] = value
                    self.assertTrue(self.errors(invalid))
        valid = deepcopy(self.ospi)
        valid["network"]["ospi"].update(address=0, size=1)
        self.assertEqual(self.errors(valid), [])

    def test_invalid_document_and_section_types(self):
        for document in (None, [], "network", {"network": None}, {"network": []}):
            with self.subTest(document=document):
                self.assertTrue(self.errors(document))
        for key in ("vela", "ospi"):
            invalid = deepcopy(self.ospi)
            invalid["network"][key] = []
            self.assertTrue(self.errors(invalid))

    def test_yaml_duplicate_and_non_string_keys_rejected(self):
        contents = (
            "network: {}\nnetwork: {}\n",
            "network:\n  name: first\n  name: second\n",
            "network:\n  vela:\n    ini: first\n    ini: second\n",
            "network:\n  1: value\n",
            "network:\n  [key]: value\n",
        )
        with tempfile.TemporaryDirectory(dir=ROOT / "tmp") as directory:
            path = Path(directory) / "network.yml"
            for content in contents:
                with self.subTest(content=content):
                    path.write_text(content, encoding="utf-8")
                    with self.assertRaises(ValueError):
                        load_yaml(path)

    def test_cli_reports_errors_and_continues_to_other_files(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "tmp") as directory:
            path = Path(directory) / "network.yml"
            valid = Path(directory) / "valid.yaml"
            valid.write_text(yaml.safe_dump(self.template), encoding="utf-8")
            contents = ("network: {}\n", "network: [\n", "", "network: {}\nnetwork: {}\n")
            for content in contents:
                with self.subTest(content=content):
                    path.write_text(content, encoding="utf-8")
                    stdout, stderr = io.StringIO(), io.StringIO()
                    with redirect_stdout(stdout), redirect_stderr(stderr):
                        self.assertEqual(main([str(path), str(valid)]), 1)
                    self.assertIn(str(path), stderr.getvalue())
                    self.assertIn(f"OK {valid}", stdout.getvalue())
            with redirect_stderr(io.StringIO()):
                self.assertEqual(main([str(path.with_name("missing.yml"))]), 1)
            path.write_text("network:\n  flash_ospi: 'false'\n", encoding="utf-8")
            stderr = io.StringIO()
            with redirect_stderr(stderr):
                self.assertEqual(main([str(path)]), 1)
            self.assertIn("$.network.flash_ospi", stderr.getvalue())

    def test_discovery_includes_both_extensions(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "tmp") as directory:
            root = Path(directory)
            projects = root / "projects"
            projects.mkdir()
            for name in ("network.yml", "network.yaml", "not-network.yml", "network.txt"):
                (projects / name).touch()
            with patch("validate_networks.ROOT", root):
                self.assertEqual([path.name for path in network_files()], ["network.yaml", "network.yml"])

    def test_cli_default_and_explicit_files_from_project_directory(self):
        script = ROOT / "scripts/validate_networks.py"
        project = ROOT / "projects/kws_micronet_l"
        for args in ([], ["model/network.yml"]):
            with self.subTest(args=args):
                result = subprocess.run([sys.executable, str(script), *args], cwd=project,
                                        capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout.count("OK "), len(self.paths) if not args else 1)

    def test_empty_scan_is_usage_error(self):
        with patch("validate_networks.network_files", return_value=[]), redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit) as result:
                main([])
        self.assertEqual(result.exception.code, 2)


if __name__ == "__main__":
    unittest.main()
