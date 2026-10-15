#!/usr/bin/env python3
# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Prepare a model-only cbuild-run file for the selected project; never flash.

MRAM projects need only the ordinary application load and produce no separate
model programming file. OSPI projects use their model/network.yml partition and the
current root ai_layer/model.pte. Program the resulting file with CMSIS flash.
"""
import argparse
from copy import deepcopy
import json
from pathlib import Path

import yaml

from project_config import DEFAULT_SOLUTION, selected_project
from create_ai_layer import sha256


class IndentedDumper(yaml.SafeDumper):
    def increase_indent(self, flow=False, indentless=False):
        return super().increase_indent(flow, False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--solution", type=Path, default=DEFAULT_SOLUTION)
    parser.add_argument("--project", help="Application name or cproject path; default: CMSIS UI selection")
    parser.add_argument("--run", type=Path, help="Application cbuild-run from the selected target (required for OSPI)")
    parser.add_argument("--output", type=Path, help="Default: out/<project>-model.cbuild-run.yml")
    parser.add_argument("--algorithm", type=Path, help="Optional locally built OSPI FLM")
    args = parser.parse_args()
    solution_file, _, project_file, config_file, network = selected_project(args.solution, args.project)
    output = args.output or solution_file.parent / f"out/{project_file.stem.removesuffix('.cproject')}-model.cbuild-run.yml"
    if not network["flash_ospi"]:
        print(f"{network['name']}: flash_ospi is false; the model is embedded in the application. Load the application into MRAM with CMSIS.")
        return
    if not args.run:
        parser.error("OSPI projects require --run <application>.cbuild-run.yml")
    model = solution_file.parent / "ai_layer/model.pte"
    report = json.loads((model.parent / "generation.json").read_text(encoding="utf-8"))
    if (report["project"] != project_file.relative_to(solution_file.parent).as_posix()
            or report["config_sha256"] != sha256(config_file)
            or report["pte_sha256"] != sha256(model)
            or report["safetensors_sha256"] != sha256(project_file.parent / network["safetensors"])
            or not report["flash_ospi"]):
        raise ValueError("Generated layer does not match the selected project/config/cache/model; regenerate it before preparing a flash file")
    run = deepcopy(yaml.safe_load(args.run.read_text(encoding="utf-8"))["cbuild-run"])
    address, partition_size = network["ospi"]["address"], network["ospi"]["size"]
    size = model.stat().st_size
    if size > partition_size:
        raise ValueError("Model exceeds its project's OSPI partition")
    algorithms = [item for item in run.get("programming", [])
                  if item["start"] <= address and address + partition_size <= item["start"] + item["size"]]
    if len(algorithms) != 1:
        raise ValueError("Expected one programming algorithm covering the entire project model partition")
    if args.algorithm:
        algorithm = args.algorithm.resolve()
        if not algorithm.is_file():
            raise ValueError("Requested flash algorithm does not exist")
        algorithms[0]["algorithm"] = algorithm.as_posix()
    else:
        # cbuild-run paths are relative to the original file, not this new file.
        algorithms[0]["algorithm"] = (args.run.resolve().parent / algorithms[0]["algorithm"]).resolve().as_posix()
    run["generated-by"] = "scripts/create_model_flash.py (model-only)"
    run["solution"] = solution_file.as_posix()
    pname = network.get("ospi", {}).get("pname", "M55_HP")
    debugger = run.setdefault("debugger", {})
    debugger.update({"start-pname": pname, "connect": "attach",
                     "load-setup": {"halt": True, "pre-reset": "off", "post-reset": "off"}})
    run["output"] = [{"file": model.as_posix(), "type": "bin", "load": "image",
                      "load-offset": address, "pname": pname,
                      "info": f"{network['name']} PTE, SHA256 {report['pte_sha256']}"}]
    run["programming"] = algorithms
    memories = run.setdefault("system-resources", {}).setdefault("memory", [])
    if not any(item["start"] <= address and address + size <= item["start"] + item["size"] for item in memories):
        memories.append({"name": "OSPI_MODEL", "access": "rx", "start": address,
                         "size": partition_size, "default": False})
    if not any(item["start"] == 0 for item in memories):
        memories.append({"name": "ITCM", "access": "rwx", "start": 0, "size": 0x40000, "default": False})
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(yaml.dump({"cbuild-run": run}, Dumper=IndentedDumper, sort_keys=False), encoding="utf-8")
    print(f"Prepared {output}: {network['name']}, {size} bytes at {address:#x}")


if __name__ == "__main__":
    main()
