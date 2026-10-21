# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Resolve an explicit or CMSIS UI-selected application and its network settings."""
import json
from pathlib import Path

import yaml

from validate_networks import load_network
from quantized_model import method_configs

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SOLUTION = ROOT / "EthosCatalog.csolution.yml"


def _project_name(path):
    return Path(path).stem.removesuffix(".cproject")


def _selected_contexts(solution_file, solution):
    """Read persisted UI target-set selection, then legacy CLI build selection.

    Use the input target set rather than generated build artefacts, which can
    still describe the previous selection before the next CMSIS build.
    """
    for workspace in solution_file.parents:
        settings_file = workspace / ".vscode/cmsis.json"
        if not settings_file.is_file():
            continue
        settings = json.loads(settings_file.read_text(encoding="utf-8"))
        key = solution_file.relative_to(workspace).as_posix().rsplit(".csolution.", 1)[0]
        selection = settings.get("targetSet", {}).get(key)
        if selection is None:
            continue
        target_name = selection.get("activeTargetType")
        target = next((t for t in solution.get("target-types", []) if t["type"] == target_name), None)
        if target is None:
            raise ValueError(f"CMSIS selected target {target_name!r} is not in {solution_file.name}")
        sets = target.get("target-set", [])
        if not sets:
            return []
        index = selection.get(target_name, -1)
        # Match the extension: an absent/out-of-range index selects the unnamed
        # set when present, otherwise the first set.
        if type(index) is not int or not 0 <= index < len(sets):
            index = next((i for i, item in enumerate(sets) if not item.get("set")), 0)
        return [image["project-context"] for image in sets[index].get("images", [])
                if "project-context" in image]
    build_set = solution_file.with_name(solution_file.name.rsplit(".csolution.", 1)[0] + ".cbuild-set.yml")
    if build_set.is_file():
        data = yaml.safe_load(build_set.read_text(encoding="utf-8"))["cbuild-set"]
        return [item["context"] for item in data["contexts"]]
    return None


def _resolve_project(solution_file, solution, project):
    projects = [(solution_file.parent / item["project"]).resolve()
                for item in solution.get("projects", []) if "project" in item]
    if project is not None:
        # Names and solution-relative/absolute paths are accepted for CLI/CI.
        path = (solution_file.parent / project).resolve()
        matches = [p for p in projects if p == path or _project_name(p) == str(project)]
    else:
        contexts = _selected_contexts(solution_file, solution)
        if contexts is None:
            matches = projects if len(projects) == 1 else []
        else:
            names = {context.split("+", 1)[0].split(".", 1)[0] for context in contexts}
            unknown = names - {_project_name(p) for p in projects}
            if unknown:
                raise ValueError(f"CMSIS selection references projects not listed in the solution: {sorted(unknown)}")
            # Utility projects such as the flash loader have no network config.
            matches = [p for p in projects if _project_name(p) in names
                       and (p.parent / "model/network.yml").is_file()]
    if len(matches) != 1:
        raise ValueError("Select exactly one application in the CMSIS UI target set, "
                         "or pass --project <name-or-cproject-path>. "
                         f"Available projects: {', '.join(_project_name(p) for p in projects)}")
    return matches[0]


def selected_project(solution_file=DEFAULT_SOLUTION, project=None):
    solution_file = Path(solution_file).resolve()
    solution = yaml.safe_load(solution_file.read_text(encoding="utf-8"))["solution"]
    project_file = _resolve_project(solution_file, solution, project)
    if not project_file.is_file():
        raise ValueError(f"Selected project does not exist: {project_file}")
    config_file = project_file.parent / "model/network.yml"
    network = load_network(config_file)
    paths = {key: network[key] for key in ("model", "quantize")}
    for name, settings in method_configs(network).items():
        paths.update({f"{name}.{key}": settings[key] for key in ("graph", "safetensors")})
    paths.update({f"assets.{name}.file": asset["file"] for name, asset in network.get("assets", {}).items()})
    for key, value in paths.items():
        path = (project_file.parent / value).resolve()
        if not path.is_relative_to(project_file.parent):
            raise ValueError(f"{config_file}: {key} must remain inside its project")
    vela = network["vela"]
    if not (project_file.parent / vela["ini"]).is_file():
        raise ValueError(f"{config_file}: Vela ini does not exist")
    return solution_file, solution, project_file, config_file, network
