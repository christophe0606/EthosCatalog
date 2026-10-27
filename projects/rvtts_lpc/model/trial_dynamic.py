#!/usr/bin/env python3
# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Record the whole acoustic dynamic-length ExecuTorch compilation attempt."""
import json
import sys
import traceback
import torch
import yaml
from model import HERE, Acoustic
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from create_ai_layer import export_model
from quantized_model import compile_spec


def main():
    from executorch.backends.arm.quantizer import EthosUQuantizer, get_symmetric_a16w8_quantization_config
    from torchao.quantization.pt2e.quantize_pt2e import prepare_pt2e, convert_pt2e
    torch.set_num_threads(1)
    solution = yaml.safe_load((ROOT / "EthosCatalog.csolution.yml").read_text())["solution"]
    spec = compile_spec(solution, HERE.parent, {"vela": {"ini": "../../.cmsis/ensemble_vela.ini", "system_config": "Ethos_U85_SRAM_OSPI", "memory_mode": "Shared_Sram"}})
    x = torch.zeros(1, 8, 169)
    dynamic = ({1: torch.export.Dim("frames", min=1, max=2048)},)
    try:
        gm = torch.export.export(Acoustic().eval(), (x,), dynamic_shapes=dynamic).module(check_guards=False)
        prepared = prepare_pt2e(gm, EthosUQuantizer(spec).set_global(get_symmetric_a16w8_quantization_config()))
        prepared(torch.randn_like(x))
        model = convert_pt2e(prepared)
        program = torch.export.export(model, (x,), dynamic_shapes=dynamic)
        pte, count = export_model(program, spec)
        report = {"status": "PASS", "pte_bytes": len(pte), "delegates": count}
    except Exception as error:
        report = {"status": "FAIL", "exception": type(error).__name__, "error": str(error), "traceback": traceback.format_exc()}
    (HERE / "dynamic_acoustic_trial.json").write_text(json.dumps(report, indent=2)+"\n")
    print(report["status"], report.get("error", report), flush=True)


if __name__ == "__main__":
    main()
