#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Select, validate and sequentially build changed catalog projects in CI."""
from __future__ import annotations

import argparse
import ast
import json
import os
from pathlib import Path, PurePosixPath
import re
import shlex
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SOLUTION = "EthosCatalog.csolution.yml"
SHARED_DIRS = {"common", "board", "thirdparty", "scripts", "schema", ".github", ".cmsis"}
SHARED_FILES = {SOLUTION, "cdefault.yml", "vcpkg-configuration.json", ".gitmodules", "AGENTS.md"}
DIRECTORIES = ("generated", "model", "nodes", "nodes_impl", "RTE")
STREAM_FILES = (
    "stream_init.cpp", "stream_init.hpp", "hardware_params.cpp", "hardware_params.h",
    "stream_types.h", "datatypes.h", "RTE/CMSIS/app_config.hpp",
    "RTE/CMSIS/stream_init.cpp", "RTE/CMSIS/stream_init.hpp",
    "RTE/CMSIS/stream_runtime_config.hpp", "generated/scheduler_app.cpp",
    "generated/scheduler_app.h", "generated/AppNodes.hpp", "generated/app_params.c",
    "generated/app_params.h", "generated/app_sizes.h", "generated/stream_settings.h",
)


class Report:
    """Persist results after each check, including when a later command crashes."""

    def __init__(self, directory):
        self.directory = Path(directory)
        self.directory.mkdir(parents=True, exist_ok=True)
        self.path = self.directory / "results.json"
        self.data = json.loads(self.path.read_text()) if self.path.exists() else {
            "projects": [], "checks": [],
        }

    def save(self):
        self.path.write_text(json.dumps(self.data, indent=2) + "\n", encoding="utf-8")

    def add(self, project, check, errors=(), log=None):
        errors = list(errors)
        self.data["checks"].append({
            "project": project, "check": check, "errors": errors, "log": log,
        })
        self.save()
        print(f"{'FAIL' if errors else 'PASS'} {project}: {check}", flush=True)
        for error in errors:
            print(f"  {error}", flush=True)
        return not errors

    def markdown(self):
        failures = [item for item in self.data["checks"] if item["errors"]]
        rows = ["# Project validation", "", f"Result: **{'FAIL' if failures else 'PASS'}**", "",
                "Projects: " + (", ".join(self.data["projects"]) or "none affected"), ""]
        if failures:
            rows += ["## Errors", ""]
            for item in failures:
                rows += [f"### {item['project']}: {item['check']}", ""]
                # Indented blocks keep arbitrary diagnostics literal in Markdown.
                for error in item["errors"]:
                    rows += ["    " + line for line in str(error).splitlines()]
                    rows.append("")
                if item["log"]:
                    rows += [f"Full output: [{item['log']}]({item['log']})", ""]
                    log = self.directory / item["log"]
                    if log.is_file():
                        rows += ["<details><summary>Command output</summary>", ""]
                        rows += ["    " + line for line in log.read_text(encoding="utf-8").splitlines()]
                        rows += ["", "</details>", ""]
        rows += ["## Checks", ""]
        for item in self.data["checks"]:
            status = "FAIL" if item["errors"] else "PASS"
            rows.append(f"- {status}: {item['project']} / {item['check']}")
        output = self.directory / "validation-errors.md"
        output.write_text("\n".join(rows) + "\n", encoding="utf-8")
        return output, bool(failures)


def affected_projects(root, paths=None):
    available = {p.name for p in (root / "projects").iterdir()
                 if p.is_dir() and not p.name.startswith((".", "__"))}
    if paths is None:
        return sorted(available)
    selected = set()
    for path in paths:
        parts = PurePosixPath(path).parts
        if not parts:
            continue
        if parts[0] in SHARED_DIRS or path in SHARED_FILES or (
            len(parts) == 1 and path.startswith(("requirements", "setup_venv"))
        ):
            return sorted(available)
        if len(parts) >= 2 and parts[0] == "projects" and parts[1] in available:
            selected.add(parts[1])
    return sorted(selected)


def git(root, *args):
    return subprocess.check_output(["git", *args], cwd=root).decode("utf-8")


def select(root, report):
    event = os.environ.get("GITHUB_EVENT_NAME", "workflow_dispatch")
    paths = None
    if event in {"push", "pull_request"}:
        payload = json.loads(Path(os.environ["GITHUB_EVENT_PATH"]).read_text(encoding="utf-8"))
        base = payload["pull_request"]["base"]["sha"] if event == "pull_request" else payload["before"]
        if base and set(base) != {"0"}:
            if not re.fullmatch(r"[0-9a-fA-F]{40,64}", base):
                raise ValueError("Invalid base commit SHA")
            try:
                if event == "pull_request":
                    base = git(root, "merge-base", base, "HEAD").strip()
                # No rename detection: both sides of a cross-project move are checked.
                paths = git(root, "diff", "--name-only", "--no-renames", "-z", base, "HEAD").split("\0")
            except subprocess.CalledProcessError:
                # A force push can remove the old commit from fetched history.
                # Conservatively check every current project, still one at a time.
                print("Base comparison unavailable; checking each current project")
    projects = affected_projects(root, paths)
    report.data["projects"] = projects
    report.save()
    if output := os.environ.get("GITHUB_OUTPUT"):
        with Path(output).open("a", encoding="utf-8") as stream:
            stream.write(f"projects={json.dumps(projects)}\n")
            stream.write(f"has_projects={'true' if projects else 'false'}\n")
    print("Affected projects: " + (", ".join(projects) or "none"))


def solution(root):
    from validate_networks import load_yaml
    return load_yaml(root / SOLUTION)["solution"]


def project_file(root, name, config):
    directory = (root / "projects" / name).resolve()
    matches = [(root / item["project"]).resolve() for item in config["projects"]
               if (root / item["project"]).resolve().parent == directory]
    if len(matches) != 1 or not matches[0].is_file():
        raise ValueError(f"Register exactly one existing cproject for projects/{name} in {SOLUTION}")
    return matches[0]


def build_targets(config, cproject):
    """Use explicit target sets, never a solution-wide or wildcard build."""
    name = cproject.name.removesuffix(".cproject.yml").removesuffix(".cproject.yaml")
    targets = []
    for target in config.get("target-types", []):
        for selection in target.get("target-set", []):
            images = selection.get("images", [])
            projects = {image["project-context"].split(".", 1)[0]
                        for image in images if "project-context" in image}
            if name not in projects:
                continue
            if projects != {name}:
                raise ValueError(f"{target['type']} target set selects multiple projects: {sorted(projects)}")
            active = target["type"]
            if selection.get("set"):
                active += "@" + selection["set"]
            targets.append(active)
    if not targets:
        raise ValueError(f"No single-project target set selects {name} in {SOLUTION}")
    return list(dict.fromkeys(targets))


def check_project(root, name, config, report):
    import yaml
    import validate_networks as networks
    import validate_benchmarks as benchmarks
    from update_catalog import read_metadata

    project = root / "projects" / name

    def check(label, operation):
        try:
            errors = list(operation())
        except (OSError, ValueError, KeyError, TypeError, SyntaxError, yaml.YAMLError) as error:
            errors = [str(error)]
        return report.add(name, label, errors)

    check("Folder hierarchy", lambda: (f"Missing directory: {directory}/" for directory in DIRECTORIES
                                       if not (project / directory).is_dir()))
    check("CMSIS Stream files", lambda: (f"Missing file: {file}" for file in STREAM_FILES
                                        if not (project / file).is_file()))

    def readme():
        path = project / "README.md"
        read_metadata(path)
        # The catalog parser checks values; also reject ambiguous duplicate keys.
        text = path.read_text(encoding="utf-8-sig").splitlines()
        end = next(i for i in range(1, len(text)) if text[i].strip() == "---")
        yaml.load("\n".join(text[1:end]), Loader=networks.UniqueKeyLoader)
        return []

    check("README YAML header", readme)

    def app():
        tree = ast.parse((project / "app.py").read_text(encoding="utf-8-sig"))
        imports = []
        for node in ast.walk(tree):
            if isinstance(node, ast.Import):
                imports.extend(alias.name for alias in node.names)
            elif isinstance(node, ast.ImportFrom) and node.level == 0 and node.module:
                imports.append(node.module)
        return [] if any(m == "cmsis_stream" or m.startswith("cmsis_stream.") for m in imports) else [
            "app.py must import cmsis_stream (an actual Python import, not a comment or string)",
        ]

    check("app.py imports CMSIS Stream", app)

    def network():
        return networks.validation_errors(networks.load_yaml(project / "model/network.yml"), networks.validator())

    check("network.yml schema", network)

    def caches():
        network = networks.load_network(project / "model/network.yml")
        errors = []
        for key, suffixes in (("graph", {".pt2"}), ("safetensors", {".safetensors", ".safetensor"})):
            path = (project / network[key]).resolve()
            if not path.is_relative_to((project / "model").resolve()):
                errors.append(f"network.{key} must refer to a file inside model/")
            elif path.suffix not in suffixes or not path.is_file() or path.stat().st_size == 0:
                errors.append(f"network.{key} must name an existing nonempty {' or '.join(sorted(suffixes))} file: {network[key]}")
        return errors

    check("Saved model graph and tensors", caches)
    for path in sorted((project / "model").glob("*benchmark*.y*ml")):
        if path.suffix in {".yml", ".yaml"}:
            check(f"{path.name} schema", lambda path=path: benchmarks.validation_errors(
                benchmarks.load_yaml(path), benchmarks.validator()))

    def linker():
        cproject = project_file(root, name, config)
        data = networks.load_yaml(cproject)["project"]
        scripts = [item["script"] for item in data.get("linker", []) if "script" in item]
        if not scripts:
            return ["The cproject must declare a project-local linker script"]
        errors = []
        for script in scripts:
            path = (project / script).resolve()
            if not path.is_relative_to(project.resolve()) or not path.is_file():
                errors.append(f"Missing project-local linker script: {script}")
        return errors

    check("Project linker script", linker)
    check("CMSIS single-project target sets", lambda: (
        build_targets(config, project_file(root, name, config)) and []))


def command(root, report, project, label, args):
    """Stream full command output and preserve it for the downloadable report."""
    index = len(report.data["checks"])
    filename = f"command-{index:03d}.log"
    print(f"+ {shlex.join(map(str, args))}", flush=True)
    with (report.directory / filename).open("w", encoding="utf-8") as log:
        log.write("$ " + shlex.join(map(str, args)) + "\n")
        try:
            process = subprocess.Popen(list(map(str, args)), cwd=root, stdout=subprocess.PIPE,
                                       stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace")
            with process.stdout:
                for line in process.stdout:
                    print(line, end="", flush=True)
                    log.write(line)
            code = process.wait()
            errors = [f"Command exited with status {code}"] if code else []
        except OSError as error:
            errors = [str(error)]
            log.write(str(error) + "\n")
    if errors:
        diagnostics = (report.directory / filename).read_text(encoding="utf-8").splitlines()[1:]
        errors.extend(dict.fromkeys(line for line in diagnostics if re.search(r"\berror\b|^FAILED:", line, re.I)))
    return report.add(project, label, errors, filename)


def build(root, names, report):
    config = solution(root)
    for label, args in (
        ("CMSIS-Toolbox version", ["cbuild", "--version"]),
        ("Build environment", ["cbuild", "list", "environment"]),
        ("Compiler toolchains", ["cbuild", "list", "toolchains", "--verbose"]),
    ):
        if not command(root, report, "Environment", label, args):
            return
    pins = [p["pack"] for p in config["packs"] if p["pack"].startswith("PyTorch::ExecuTorch@")]
    if len(pins) != 1:
        raise ValueError("Pin exactly one ExecuTorch runtime pack in the solution")
    # The exporter reads this pack before an AI layer exists. Other missing packs
    # are installed by cbuild --packs, after the layer has been generated.
    if not command(root, report, "Environment", "ExecuTorch runtime pack",
                   ["cpackget", "add", "-a", pins[0]]):
        return
    for name in names:
        try:
            cproject = project_file(root, name, config)
            targets = build_targets(config, cproject)
        except (ValueError, KeyError, TypeError) as error:
            report.add(name, "Build selection", [str(error)])
            continue
        # Deliberately sequential: ai_layer belongs to this project until every
        # one of its target sets has finished building. Never reuse another's.
        graph_ok = command(root, report, name, "Generate CMSIS Stream graph",
                           [sys.executable, cproject.parent / "app.py"])
        layer_ok = command(root, report, name, "Generate project AI layer",
                           [sys.executable, root / "scripts/create_ai_layer.py",
                            "--solution", root / SOLUTION, "--project", cproject.relative_to(root)])
        if not graph_ok or not layer_ok:
            report.add(name, "LLVM build", ["Build skipped because project generation failed"])
            continue
        for target in targets:
            command(root, report, name, f"LLVM build: {target}",
                    ["cbuild", SOLUTION, "--active", target, "--toolchain", "CLANG", "--packs"])


def finish(report):
    steps = json.loads(os.environ.get("CI_STEPS", "{}"))
    for name, step in steps.items():
        if step.get("outcome") == "failure":
            report.add("Workflow", name, [f"Step '{name}' failed. See the Actions job log."])
    if report.data["projects"] and steps and steps.get("build", {}).get("outcome") == "skipped":
        report.add("Workflow", "LLVM builds", ["Builds were skipped because a required setup step failed."])
    _, failed = report.markdown()
    if summary := os.environ.get("GITHUB_STEP_SUMMARY"):
        with Path(summary).open("a", encoding="utf-8") as stream:
            stream.write(f"# Project validation\n\nResult: **{'FAIL' if failed else 'PASS'}**\n\n")
            stream.write("Download **project-validation-report** for `validation-errors.md` and command logs.\n\n")
            for item in report.data["checks"]:
                if item["errors"]:
                    stream.write(f"- FAIL: {item['project']} / {item['check']}\n")
    return int(failed)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("select", "validate", "build", "report"))
    parser.add_argument("--report-dir", type=Path, default=ROOT / "ci-reports")
    args = parser.parse_args(argv)
    report = Report(args.report_dir)
    first_check = len(report.data["checks"])
    try:
        if args.action == "select":
            select(ROOT, report)
        elif args.action == "validate":
            config = solution(ROOT)
            for name in report.data["projects"]:
                check_project(ROOT, name, config, report)
        elif args.action == "build":
            build(ROOT, report.data["projects"], report)
        else:
            return finish(report)
    except Exception as error:
        report.add("Workflow", args.action, [f"{type(error).__name__}: {error}"])
    finally:
        report.markdown()
    return int(any(item["errors"] for item in report.data["checks"][first_check:]))


if __name__ == "__main__":
    raise SystemExit(main())
