"""Shared cache I/O and compiler configuration; quantization belongs to projects."""
import hashlib
from pathlib import Path


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def compile_spec(solution, project_dir, network):
    from executorch.backends.arm.ethosu import EthosUCompileSpec
    npu = solution["mlops"]["npu"]
    vela = network["vela"]
    kwargs = dict(target=f"{npu['type'].lower()}-{npu['macs']}",
                  system_config=vela["system_config"], memory_mode=vela["memory_mode"],
                  config_ini=str((project_dir / vela["ini"]).resolve()))
    print(f"[ai_layer] Vela: {kwargs}", flush=True)
    return EthosUCompileSpec(**kwargs)


def save_quantized_program(program, target, project_dir, network):
    """Save the project's quantized graph and tensors before delegate compilation."""
    import torch
    from safetensors.torch import save_file
    weights_file = project_dir / network["safetensors"]
    graph_file = project_dir / network["graph"]
    tensors = {f"state.{key}": value.detach().cpu().contiguous().clone()
               for key, value in program.state_dict.items()}
    tensors.update({f"constant.{key}": value.detach().cpu().contiguous().clone()
                    for key, value in program.constants.items() if isinstance(value, torch.Tensor)})
    weights_file.parent.mkdir(parents=True, exist_ok=True)
    graph_file.parent.mkdir(parents=True, exist_ok=True)
    torch.export.save(program, graph_file)
    save_file(tensors, weights_file, metadata={
        "format": "ethos-catalog-quantized-v1", "target": target,
        "graph_sha256": sha256(graph_file),
        "source_sha256": sha256(project_dir / network["model"]),
        "quantizer_sha256": sha256(project_dir / network["quantize"]),
    })


def load_quantized_program(target, project_dir, network):
    """Restore a quantized cache without importing its source model or quantizer."""
    import torch
    from safetensors import safe_open
    from safetensors.torch import load_file
    # Register decomposed quantization operators before loading the saved graph.
    import torchao.quantization.pt2e.quantize_pt2e  # noqa: F401
    weights_file = project_dir / network["safetensors"]
    graph_file = project_dir / network["graph"]
    if not weights_file.is_file() or not graph_file.is_file():
        raise ValueError("Quantized cache needs safetensors and its companion .pt2 graph; run --quantize")
    with safe_open(weights_file, framework="pt", device="cpu") as cache:
        metadata = cache.metadata() or {}
    if metadata.get("format") != "ethos-catalog-quantized-v1" or metadata.get("target") != target:
        raise ValueError("Quantized cache format/accelerator differs; run --quantize")
    if metadata.get("graph_sha256") != sha256(graph_file):
        raise ValueError("Quantized graph differs from its safetensors cache; run --quantize")
    program = torch.export.load(graph_file)
    # Detach from the file mapping so a project quantizer can replace the cache
    # later in the same process on Windows.
    tensors = {key: value.clone() for key, value in load_file(weights_file).items()}
    state = {key.removeprefix("state."): value for key, value in tensors.items() if key.startswith("state.")}
    constants = {key.removeprefix("constant."): value for key, value in tensors.items()
                 if key.startswith("constant.")}
    expected_constants = {key: value for key, value in program.constants.items() if isinstance(value, torch.Tensor)}
    if set(state) != set(program.state_dict) or set(constants) != set(expected_constants):
        raise ValueError("Quantized cache state/constants differ from graph; run --quantize")
    for cached, original in ((state, program.state_dict), (constants, expected_constants)):
        for key, value in cached.items():
            if value.shape != original[key].shape or value.dtype != original[key].dtype:
                raise ValueError(f"Quantized cache shape/type differs for {key}; run --quantize")
    # Exported parameter inputs must remain Parameters when torch un-lifts them.
    for key, value in state.items():
        original = program.state_dict[key]
        program.state_dict[key] = (torch.nn.Parameter(value, requires_grad=original.requires_grad)
                                   if isinstance(original, torch.nn.Parameter) else value)
    program.constants.update(constants)
    print(f"[ai_layer] reusing {weights_file} (no quantization)", flush=True)
    return program
