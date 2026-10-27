#!/usr/bin/env python3
# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Record the whole-prosody ExecuTorch partition attempt before decomposition."""
import json
from pathlib import Path
import sys
import traceback
import torch
import yaml
from model import HERE, Prosody, sample, build_inputs
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from quantized_model import compile_spec
from create_ai_layer import export_model, components, executorch_pack


def main():
    from executorch.backends.arm.quantizer import EthosUQuantizer, get_symmetric_a16w8_quantization_config
    from torchao.quantization.pt2e.quantize_pt2e import prepare_pt2e, convert_pt2e
    torch.set_num_threads(1)
    solution = yaml.safe_load((ROOT / "EthosCatalog.csolution.yml").read_text())["solution"]
    spec = compile_spec(solution, HERE.parent, {"vela": {"ini": "../../.cmsis/ensemble_vela.ini", "system_config": "Ethos_U85_SRAM_OSPI", "memory_mode": "Shared_Sram"}})
    inputs = [build_inputs(*sample(seed, 8)) for seed in range(8)]
    try:
        model = torch.export.export(Prosody().eval(), (inputs[0],)).module(check_guards=False)
        q = EthosUQuantizer(spec).set_global(get_symmetric_a16w8_quantization_config())
        prepared = prepare_pt2e(model, q)
        for x in inputs:
            prepared(x)
        converted = convert_pt2e(prepared)
        pte, count = export_model(torch.export.export(converted, (inputs[0],)), spec)
        _, pack = executorch_pack(solution, ROOT)
        runtime, operators = components(pte, pack)
        result = {"status": "PASS", "delegates": count, "bytes": len(pte), "operators": operators}
    except Exception as error:
        result = {"status": "FAIL", "exception": type(error).__name__, "error": str(error), "traceback": traceback.format_exc()}
    (HERE / "whole_prosody_trial.json").write_text(json.dumps(result, indent=2) + "\n")
    print(result["status"], result.get("error", result), flush=True)


if __name__ == "__main__":
    main()
