"""Exercise CI selection, project validation, failure reports and build isolation."""
from pathlib import Path
import json
import os
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import yaml

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import ci_projects as ci


class CIProjects(unittest.TestCase):
    def setUp(self):
        (ROOT / "tmp").mkdir(exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(dir=ROOT / "tmp")
        self.root = Path(self.temp.name)
        self.addCleanup(self.temp.cleanup)
        self.report = ci.Report(self.root / "reports")
        self.config = yaml.safe_load((ROOT / ci.SOLUTION).read_text())["solution"]

    def project(self, name="demo"):
        directory = self.root / "projects" / name
        for folder in ci.DIRECTORIES:
            (directory / folder).mkdir(parents=True, exist_ok=True)
        for file in ci.STREAM_FILES:
            (directory / file).parent.mkdir(parents=True, exist_ok=True)
            (directory / file).write_text("// fixture\n")
        (directory / "README.md").write_text("---\ncategory: audio\ndescription: A demo.\n---\n")
        (directory / "app.py").write_text("from cmsis_stream.cg.scheduler import Graph\n")
        (directory / "linker.sct.src").write_text("/* fixture */\n")
        (directory / "model/graph.pt2").write_bytes(b"graph")
        (directory / "model/weights.safetensors").write_bytes(b"weights")
        network = yaml.safe_load((ROOT / "projects/template_mram/model/network.yml").read_text())
        network["network"]["graph"] = "model/graph.pt2"
        network["network"]["safetensors"] = "model/weights.safetensors"
        (directory / "model/network.yml").write_text(yaml.safe_dump(network))
        cproject = directory / f"{name}.cproject.yml"
        cproject.write_text("project:\n  linker:\n  - script: linker.sct.src\n")
        if not hasattr(self, "fixture_projects"):
            self.fixture_projects = []
        self.fixture_projects.append(
            {"project": cproject.relative_to(self.root).as_posix()},
        )
        self.config["projects"] = self.fixture_projects
        self.config["target-types"] = [{"type": "Board", "target-set": [
            {"set": f"{entry['project'].split('/')[1]}_Release", "images": [
                {"project-context": f"{entry['project'].split('/')[1]}.Release"},
            ]} for entry in self.config["projects"]
        ]}]
        (self.root / ci.SOLUTION).write_text(yaml.safe_dump({"solution": self.config}))
        return directory

    def errors(self):
        return "\n".join(error for item in self.report.data["checks"] for error in item["errors"])

    def test_select_only_changed_project_and_ignore_deleted_project(self):
        self.project("first")
        self.project("second")
        paths = ["projects/first/README.md", "projects/deleted/app.py", "documentation/HOWTO.md"]
        self.assertEqual(ci.affected_projects(self.root, paths), ["first"])
        self.assertEqual(ci.affected_projects(self.root, ["README.md"]), [])

    def test_shared_changes_and_manual_run_include_each_project(self):
        self.project("first")
        self.project("second")
        for paths in (None, ["common/node.hpp"], ["schema/network.schema.yml"], ["setup_venv.py"]):
            self.assertEqual(ci.affected_projects(self.root, paths), ["first", "second"])

    def git(self, *args):
        return subprocess.check_output(["git", *args], cwd=self.root, text=True).strip()

    def init_git(self):
        self.git("init", "--initial-branch=main")
        self.git("config", "user.email", "ci@example.invalid")
        self.git("config", "user.name", "CI test")
        self.git("add", "projects", ci.SOLUTION)
        self.git("commit", "-qm", "Initial")
        return self.git("rev-parse", "HEAD")

    def select_event(self, event, payload):
        path = self.root / "event.json"
        path.write_text(json.dumps(payload))
        with patch.dict(os.environ, {"GITHUB_EVENT_NAME": event, "GITHUB_EVENT_PATH": str(path),
                                     "GITHUB_OUTPUT": str(self.root / "output")}):
            ci.select(self.root, self.report)
        return self.report.data["projects"]

    def test_push_includes_every_commit_and_both_sides_of_move(self):
        first = self.project("first")
        second = self.project("second")
        before = self.init_git()
        (first / "README.md").write_text("changed")
        self.git("add", ".")
        self.git("commit", "-qm", "First change")
        self.git("mv", str(first / "app.py"), str(second / "moved.py"))
        self.git("commit", "-qm", "Move between projects")
        self.assertEqual(self.select_event("push", {"before": before}), ["first", "second"])

    def test_pr_uses_merge_base_without_unrelated_base_changes(self):
        first = self.project("first")
        second = self.project("second")
        self.init_git()
        self.git("checkout", "-qb", "feature")
        (first / "README.md").write_text("feature change")
        self.git("add", ".")
        self.git("commit", "-qm", "Feature")
        self.git("checkout", "-q", "main")
        (second / "README.md").write_text("base change")
        self.git("add", ".")
        self.git("commit", "-qm", "Advance base")
        base = self.git("rev-parse", "HEAD")
        self.git("checkout", "-q", "feature")
        self.assertEqual(self.select_event("pull_request", {"pull_request": {"base": {"sha": base}}}), ["first"])

    def test_initial_push_selects_all(self):
        self.project()
        self.assertEqual(self.select_event("push", {"before": "0" * 40}), ["demo"])

    def test_unavailable_old_commit_conservatively_checks_each_project(self):
        self.project("first")
        self.project("second")
        with patch.object(ci, "git", side_effect=subprocess.CalledProcessError(128, "git")):
            self.assertEqual(self.select_event("push", {"before": "a" * 40}), ["first", "second"])

    def test_valid_project_without_optional_benchmark(self):
        self.project()
        ci.check_project(self.root, "demo", self.config, self.report)
        self.assertEqual(self.errors(), "")

    def test_reports_independent_validation_errors_together(self):
        directory = self.project()
        (directory / "nodes").rmdir()
        (directory / "stream_init.cpp").unlink()
        (directory / "hardware_params.h").unlink()
        (directory / "README.md").write_text("# No YAML header\n")
        (directory / "app.py").write_text('# import cmsis_stream\nx = "import cmsis_stream"\n')
        (directory / "model/graph.pt2").unlink()
        (directory / "model/weights.safetensors").unlink()
        (directory / "model/benchmark.yml").write_text("benchmarks: {}\n")
        (directory / "linker.sct.src").unlink()
        ci.check_project(self.root, "demo", self.config, self.report)
        errors = self.errors()
        for text in ("nodes/", "stream_init.cpp", "hardware_params.h", "YAML header", "import cmsis_stream",
                     "graph.pt2", "weights.safetensors", "required property", "linker.sct.src"):
            self.assertIn(text, errors)
        output, failed = self.report.markdown()
        self.assertTrue(failed)
        self.assertIn("## Errors", output.read_text())
        self.assertIn("graph.pt2", ci.Report(self.report.directory).markdown()[0].read_text())

    def test_cache_must_be_the_configured_file_inside_model(self):
        directory = self.project()
        (directory / "outside.pt2").write_bytes(b"graph")
        path = directory / "model/network.yml"
        doc = yaml.safe_load(path.read_text())
        doc["network"]["graph"] = "outside.pt2"
        path.write_text(yaml.safe_dump(doc))
        ci.check_project(self.root, "demo", self.config, self.report)
        self.assertIn("inside model/", self.errors())

    def test_duplicate_yaml_keys_and_invalid_network_schema_fail(self):
        directory = self.project()
        (directory / "README.md").write_text("---\ncategory: a\ncategory: b\ndescription: demo\n---\n")
        (directory / "model/network.yml").write_text("network:\n  flash_ospi: wrong\n")
        ci.check_project(self.root, "demo", self.config, self.report)
        self.assertIn("duplicate key", self.errors())
        self.assertIn("is not of type 'boolean'", self.errors())

    def test_ci_rejects_invalid_quantization_provenance(self):
        directory = self.project()
        path = directory / "model/network.yml"
        document = yaml.safe_load(path.read_text())
        document["network"]["quantization"]["quantization"] = "fake"
        path.write_text(yaml.safe_dump(document))
        ci.check_project(self.root, "demo", self.config, self.report)
        self.assertIn("$.network.quantization.quantization", self.errors())

    def test_reject_target_set_selecting_multiple_projects(self):
        directory = self.project()
        self.config["target-types"][0]["target-set"][0]["images"].append({"project-context": "other.Release"})
        with self.assertRaisesRegex(ValueError, "multiple projects"):
            ci.build_targets(self.config, directory / "demo.cproject.yml")

    def test_build_is_sequential_and_forces_clang_for_one_project_at_a_time(self):
        self.project("first")
        self.project("second")
        calls = []

        def record(root, report, project, label, args):
            calls.append((project, label, list(map(str, args))))
            return True

        with patch.object(ci, "command", side_effect=record):
            ci.build(self.root, ["first", "second"], self.report)
        self.assertEqual([(p, label) for p, label, _ in calls], [
            ("Environment", "CMSIS-Toolbox version"),
            ("Environment", "Build environment"),
            ("Environment", "Compiler toolchains"),
            ("Environment", "ExecuTorch runtime pack"),
            ("first", "Generate CMSIS Stream graph"), ("first", "Generate project AI layer"),
            ("first", "LLVM build: Board@first_Release"),
            ("second", "Generate CMSIS Stream graph"), ("second", "Generate project AI layer"),
            ("second", "LLVM build: Board@second_Release"),
        ])
        for _, label, args in calls:
            if label.startswith("LLVM build:"):
                self.assertEqual(args[-3:], ["--toolchain", "CLANG", "--packs"])
                self.assertIn("--active", args)

    def test_failed_layer_skips_its_build_and_next_project_gets_own_layer(self):
        self.project("first")
        self.project("second")
        calls = []

        def run(root, report, project, label, args):
            calls.append((project, label))
            return (project, label) != ("first", "Generate project AI layer")

        with patch.object(ci, "command", side_effect=run):
            ci.build(self.root, ["first", "second"], self.report)
        self.assertNotIn(("first", "LLVM build: Board@first_Release"), calls)
        self.assertEqual(calls[-2:], [("second", "Generate project AI layer"),
                                     ("second", "LLVM build: Board@second_Release")])

    def test_build_failure_does_not_prevent_remaining_targets_or_projects(self):
        self.project("first")
        self.project("second")
        self.config["target-types"][0]["target-set"].insert(1, {
            "set": "first_Debug", "images": [{"project-context": "first.Debug"}],
        })
        (self.root / ci.SOLUTION).write_text(yaml.safe_dump({"solution": self.config}))
        calls = []

        def run(root, report, project, label, args):
            calls.append((project, label))
            return (project, label) != ("first", "LLVM build: Board@first_Release")

        with patch.object(ci, "command", side_effect=run):
            ci.build(self.root, ["first", "second"], self.report)
        self.assertIn(("first", "LLVM build: Board@first_Debug"), calls)
        self.assertEqual(calls[-2:], [("second", "Generate project AI layer"),
                                     ("second", "LLVM build: Board@second_Release")])

    def test_failed_command_report_embeds_actual_diagnostics(self):
        ok = ci.command(self.root, self.report, "demo", "compile", [
            sys.executable, "-c", "import sys; print('error: undefined symbol'); sys.exit(2)",
        ])
        self.assertFalse(ok)
        output, failed = self.report.markdown()
        self.assertTrue(failed)
        self.assertIn("error: undefined symbol", output.read_text())
        self.assertIn("status 2", output.read_text())

    def test_missing_tool_reports_failure(self):
        self.assertFalse(ci.command(self.root, self.report, "demo", "compile", ["nonexistent-ci-tool"]))

    def test_setup_failures_fail_final_report_even_when_build_skipped(self):
        self.report.data["projects"] = ["demo"]
        self.report.save()
        steps = {"vcpkg": {"outcome": "failure"}, "build": {"outcome": "skipped"}}
        with patch.dict(os.environ, {"CI_STEPS": json.dumps(steps), "GITHUB_STEP_SUMMARY": str(self.root / "summary")}):
            self.assertEqual(ci.finish(self.report), 1)
        self.assertIn("vcpkg", self.errors())
        self.assertIn("Builds were skipped", self.errors())
        self.assertIn("**FAIL**", (self.root / "summary").read_text())

    def test_documentation_only_change_passes_with_no_build(self):
        with patch.dict(os.environ, {"CI_STEPS": json.dumps({"build": {"outcome": "skipped"}})}):
            self.assertEqual(ci.finish(self.report), 0)

    def test_error_report_works_when_python_dependencies_could_not_be_installed(self):
        # -S disables site-packages: reporting must use the standard library only.
        result = subprocess.run([
            sys.executable, "-S", str(ROOT / "scripts/ci_projects.py"), "report",
            "--report-dir", str(self.report.directory),
        ], capture_output=True, text=True, env=dict(
            os.environ, CI_STEPS=json.dumps({"executorch": {"outcome": "failure"}}),
            GITHUB_STEP_SUMMARY=str(self.root / "summary"),
        ))
        self.assertEqual(result.returncode, 1, result.stderr)
        text = (self.report.directory / "validation-errors.md").read_text()
        self.assertIn("**FAIL**", text)
        self.assertIn("executorch", text)


if __name__ == "__main__":
    unittest.main()
