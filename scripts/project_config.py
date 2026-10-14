"""Resolve the sole selected application and its project-local network settings."""
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SOLUTION = ROOT / "EthosCatalog.csolution.yml"


def selected_project(solution_file=DEFAULT_SOLUTION):
    solution_file = Path(solution_file).resolve()
    solution = yaml.safe_load(solution_file.read_text(encoding="utf-8"))["solution"]
    projects = solution.get("projects", [])
    if len(projects) != 1:
        raise ValueError("Select exactly one application project in the csolution before generating its AI layer")
    project_file = (solution_file.parent / projects[0]["project"]).resolve()
    if not project_file.is_file():
        raise ValueError(f"Selected project does not exist: {project_file}")
    config_file = project_file.parent / "model/network.yml"
    network = yaml.safe_load(config_file.read_text(encoding="utf-8"))["network"]
    if not isinstance(network.get("flash_ospi"), bool):
        raise ValueError(f"{config_file}: flash_ospi must be true or false")
    for key in ("name", "model", "quantize", "safetensors", "graph"):
        if not isinstance(network.get(key), str) or not network[key]:
            raise ValueError(f"{config_file}: {key} must be a nonempty string")
    for key in ("model", "quantize", "safetensors", "graph"):
        path = (project_file.parent / network[key]).resolve()
        if not path.is_relative_to(project_file.parent):
            raise ValueError(f"{config_file}: {key} must remain inside its project")
    vela = network.get("vela", {})
    for key in ("ini", "system_config", "memory_mode"):
        if not isinstance(vela.get(key), str) or not vela[key]:
            raise ValueError(f"{config_file}: vela.{key} must be a nonempty string")
    if not (project_file.parent / vela["ini"]).is_file():
        raise ValueError(f"{config_file}: Vela ini does not exist")
    if network["flash_ospi"]:
        for key in ("address", "size"):
            if type(network.get("ospi", {}).get(key)) is not int:
                raise ValueError(f"{config_file}: OSPI projects require integer ospi.{key}")
        if network["ospi"]["size"] <= 0 or network["ospi"]["address"] < 0:
            raise ValueError("Invalid OSPI model partition")
    return solution_file, solution, project_file, config_file, network
