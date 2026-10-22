#!/usr/bin/env python3
# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Generate project-local ai_layer for the application selected in the CMSIS UI.

Run with `uv run scripts/create_ai_layer.py [--quantize]`.
First-run quantization and --quantize call the script configured in the selected
project's model/network.yml. That project owns model loading, calibration and
quantization policy. This script compiles the saved quantized graph/weights
with Vela and emits the project's layer; it never generates firmware test data.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import zlib

from project_config import DEFAULT_SOLUTION, selected_project
from quantized_model import cache_provenance, compile_spec, load_quantized_program, method_configs, model_assets, sha256

PACK = "PyTorch::ExecuTorch"
SYMBOL = "model_pte"


def pack_root(solution_dir):
    if value := os.environ.get("CMSIS_PACK_ROOT"):
        return Path(value)
    # The extension's exported build environment selects the installed packs.
    import yaml
    environment = solution_dir / ".cmsis/tools-environment.yml"
    if environment.is_file():
        value = yaml.safe_load(environment.read_text())["cmsis-tools-environment"]["environment"]["variables"].get("CMSIS_PACK_ROOT")
        if value:
            return Path(value)
    if os.name == "nt":
        return Path(os.environ.get("LOCALAPPDATA", Path.home() / "AppData/Local")) / "Arm/Packs"
    return Path(os.environ.get("XDG_CACHE_HOME", Path.home() / ".cache")) / "arm/packs"


def executorch_pack(solution, solution_dir):
    pins = [p["pack"].partition("@")[2] for p in solution["packs"] if p["pack"].startswith(PACK + "@")]
    if len(pins) != 1 or not re.fullmatch(r"\d+\.\d+\.\d+", pins[0]):
        raise ValueError("Pin exactly one ExecuTorch pack version in the csolution")
    version = pins[0]
    path = pack_root(solution_dir) / "PyTorch/ExecuTorch" / version
    if not path.is_dir():
        raise ValueError(f"Install the solution's ExecuTorch pack through CMSIS first: {path}")
    from importlib.metadata import version as package_version
    if package_version("executorch") != version:
        raise ValueError("Python ExecuTorch version must match the csolution's runtime pack")
    return version, path


def prepare_quantized_model(solution_file, project_file, network, force=False):
    """Call the selected project's quantizer on first use or explicit request."""
    project_dir = project_file.parent
    caches = method_configs(network)
    if force or any(not (project_dir / settings["safetensors"]).is_file() for settings in caches.values()):
        script = project_dir / network["quantize"]
        if not script.is_file():
            raise ValueError(f"Project quantization script does not exist: {script}")
        print(f"[ai_layer] calling project quantizer: {script}", flush=True)
        subprocess.run([sys.executable, str(script), "--solution", str(solution_file),
                        "--project", str(project_file)],
                       cwd=project_dir, check=True)


def export_model(program, spec, *, cpu_methods=(), inspection_callback=None):
    """Lower one graph or named methods into one PTE.

    CPU-only methods, such as a token embedding lookup, must be named explicitly.
    Every other method must produce an Ethos delegate.
    An optional callback can record the lowered edge graphs before serialization.
    """
    # Register CPU quantize/dequantize out variants used at float boundaries.
    import executorch.kernels.quantized  # noqa: F401
    from executorch.backends.arm.ethosu import EthosUPartitioner
    from executorch.exir import EdgeCompileConfig, ExecutorchBackendConfig, to_edge_transform_and_lower
    methods = program if isinstance(program, dict) else {"forward": program}
    cpu_methods = set(cpu_methods)
    unknown = cpu_methods - methods.keys()
    if unknown:
        raise ValueError(f"Unknown CPU-only methods: {sorted(unknown)}")
    partitioners = {name: [] if name in cpu_methods else [EthosUPartitioner(spec)]
                    for name in methods}
    edge = to_edge_transform_and_lower(methods, partitioner=partitioners,
                                      compile_config=EdgeCompileConfig(_check_ir_validity=False))
    counts = {name: sum(node.op == "call_function" and "executorch_call_delegate" in str(node.target)
                       for node in edge.exported_program(name).graph.nodes)
              for name in edge.methods}
    missing = sorted(name for name, count in counts.items() if not count and name not in cpu_methods)
    if missing:
        raise ValueError(f"No Ethos-U delegate was generated for methods: {missing}")
    delegated = sum(counts.values())
    if not delegated:
        raise ValueError("No Ethos-U delegate was generated")
    if inspection_callback is not None:
        inspection_callback(edge)
    output = edge.to_executorch(ExecutorchBackendConfig(extract_delegate_segments=False))
    return bytes(output.buffer), delegated


def components(pte, pack):
    from executorch.exir._serialize._program import deserialize_pte_binary
    available = set(re.findall(r'Csub="([^"]+)"', next(pack.glob("*.pdsc")).read_text()))
    family = {"aten": "Portable", "quantized_decomposed": "Quantized", "cortex_m": "Cortex-M",
              "dim_order_ops": "Portable"}
    aliases = {"_softmax": "softmax", "_clone_dim_order": "clone_dim_order"}
    program = deserialize_pte_binary(pte).program
    required = {operator.name for plan in program.execution_plan for operator in plan.operators}
    selected, unknown = set(), []
    for name in sorted(required):
        ns, _, op = name.partition("::")
        if ns not in family:
            unknown.append(name)
            continue
        candidates = [f"{family[ns]} {op}", f"{family[ns]} {aliases.get(op, op)}",
                      f"{family[ns]} {re.sub(r'_(per_tensor|per_channel|byte|copy)$', '', op)}"]
        match = next((c for c in candidates if c in available), None)
        if match:
            selected.add(match)
        else:
            unknown.append(f"{ns}::{op}")
    if unknown:
        raise ValueError(f"Runtime pack has no operator components for {unknown}")
    return ["Runtime", "Kernel Utils", "Kernel Registration", "Backend EthosU"], sorted(selected)


def c_array(pte, network):
    prefix = ('// Copyright 2026 Arm Limited and/or its affiliates.\n'
              '// SPDX-License-Identifier: Apache-2.0\n'
              '// Generated by scripts/create_ai_layer.py -- do not edit.\n#include "model_pte.h"\n')
    if network["flash_ospi"]:
        if len(pte) > network["ospi"]["size"]:
            raise ValueError("PTE does not fit the project's OSPI partition")
        return prefix + f'const unsigned char *const {SYMBOL} = (const unsigned char *)0x{network["ospi"]["address"]:08x}UL;\nconst unsigned long {SYMBOL}_size = {len(pte)}UL;\n'
    rows = [", ".join(f"0x{b:02x}" for b in pte[i:i + 16]) for i in range(0, len(pte), 16)]
    return prefix + f'__attribute__((aligned(16), section(".rodata.model_pte"))) const unsigned char {SYMBOL}[] = {{\n  ' + ",\n  ".join(rows) + f'\n}};\nconst unsigned long {SYMBOL}_size = sizeof({SYMBOL});\n'


def header(network, pte, assets=None):
    declaration = f"*const {SYMBOL}" if network["flash_ospi"] else f"{SYMBOL}[]"
    asset_defines = "".join(
        f'#define MODEL_ASSET_{name.upper()}_ADDRESS 0x{asset["address"]:08x}UL\n'
        f'#define MODEL_ASSET_{name.upper()}_SIZE_BYTES {asset["size"]}U\n'
        f'#define MODEL_ASSET_{name.upper()}_CRC32 0x{asset["crc32"]:08x}U\n'
        for name, asset in (assets or {}).items())
    return f'''// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
// Generated by scripts/create_ai_layer.py -- do not edit.
#pragma once
#define MODEL_PTE_SIZE_BYTES {len(pte)}U
#define MODEL_PTE_CRC32 0x{zlib.crc32(pte):08x}U
{asset_defines}#ifdef __cplusplus
extern "C" {{
#endif
extern const unsigned char {declaration};
extern const unsigned long {SYMBOL}_size;
#ifdef __cplusplus
}}
#endif
'''


def clayer(network, runtime, operators, version):
    import yaml
    operator_components = []
    for name in operators:
        component = {"component": f"Machine Learning:ExecuTorch Operators:{name}"}
        if name == "Portable gelu":
            # This kernel explicitly handles IEEE infinities. Preserve those
            # semantics when the application enables AC6 -ffast-math globally.
            component["misc"] = [{"for-compiler": "AC6", "C-CPP": ["-fno-fast-math"]}]
        operator_components.append(component)
    layer = {"type": "AI", "description": network["name"], "packs": [{"pack": f"{PACK}@{version}"}],
             "define": [{"ET_LOG_ENABLED": 0}], "add-path": ["."],
             "components": ([{"component": f"Machine Learning:ExecuTorch:{c}"} for c in runtime]
                            + operator_components),
             "groups": [{"group": f"Model {network['name']}", "files": [{"file": f"./{name}"} for name in
                        ("model_pte.c", "model_pte.h")]}]}
    return "# Generated by scripts/create_ai_layer.py -- do not edit.\n" + yaml.safe_dump({"layer": layer}, sort_keys=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mlops", nargs="?", type=Path, help="Legacy path used only to locate the csolution; .cbuild-mlops.yml contents are not read")
    parser.add_argument("--solution", type=Path, default=DEFAULT_SOLUTION)
    parser.add_argument("--project", help="Application name or cproject path; default: CMSIS UI selection")
    parser.add_argument("--quantize", action="store_true", help="Call the selected project quantization script to replace its cache")
    args = parser.parse_args()
    if args.mlops:
        if not args.mlops.name.endswith(".cbuild-mlops.yml"):
            parser.error("positional path must end with .cbuild-mlops.yml")
        args.solution = args.mlops.with_name(args.mlops.name.replace(".cbuild-mlops.yml", ".csolution.yml"))
    solution_file, solution, project_file, config_file, network = selected_project(args.solution, args.project)
    print(f"[ai_layer] selected project: {project_file}", flush=True)
    version, pack = executorch_pack(solution, solution_file.parent)
    prepare_quantized_model(solution_file, project_file, network, args.quantize)
    spec = compile_spec(solution, project_file.parent, network)
    if "methods" in network:
        settings = method_configs(network)
        programs = {name: load_quantized_program(spec.target, project_file.parent, method)
                    for name, method in settings.items()}
        cpu_methods = [name for name, method in settings.items() if not method.get("delegate", True)]
        pte, delegated = export_model(programs, spec, cpu_methods=cpu_methods)
    else:
        program = load_quantized_program(spec.target, project_file.parent, network)
        pte, delegated = export_model(program, spec)
    runtime, operators = components(pte, pack)
    assets = model_assets(project_file.parent, network, len(pte))
    model_source = c_array(pte, network)
    model_header = header(network, pte, assets)
    layer_dir = project_file.parent / "ai_layer"
    layer_dir.mkdir(exist_ok=True)
    (layer_dir / "model.pte").write_bytes(pte)
    (layer_dir / "model_pte.c").write_text(model_source, encoding="utf-8", newline="\n")
    (layer_dir / "model_pte.h").write_text(model_header, encoding="utf-8", newline="\n")
    (layer_dir / "ai_layer.clayer.yml").write_text(clayer(network, runtime, operators, version), encoding="utf-8", newline="\n")
    report = {"project": project_file.relative_to(solution_file.parent).as_posix(),
              "config_sha256": sha256(config_file), "pte_sha256": hashlib.sha256(pte).hexdigest(),
              **cache_provenance(project_file.parent, network),
              "assets": assets,
              "flash_ospi": network["flash_ospi"], "vela": network["vela"],
              "delegates": delegated, "operators": operators, "pte_bytes": len(pte)}
    (layer_dir / "generation.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"[ai_layer] wrote {layer_dir}: {len(pte)} bytes, {delegated} Ethos-U delegate(s), {operators}")


if __name__ == "__main__":
    # FlatBuffer serialization invokes a compiler and cleans scratch files. Keep
    # CLI scratch files inside this repository in restricted environments.
    scratch = Path(__file__).resolve().parents[1] / "tmp"
    scratch.mkdir(exist_ok=True)
    tempfile.tempdir = str(scratch)
    main()
