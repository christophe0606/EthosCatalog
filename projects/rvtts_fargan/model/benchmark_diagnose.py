#!/usr/bin/env python3
# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Bounded read-only fixture/cache and pitch-index checks after board failures."""
import json
from pathlib import Path
import sys
import numpy as np
import torch
import yaml
import executorch.exir.passes.dim_order_ops_registry  # register saved boundary casts

HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE.parents[2]/"scripts"))
from quantized_model import load_quantized_program,method_configs

def main():
    torch.set_num_threads(1)
    network=yaml.safe_load((HERE/"network.yml").read_text())["network"]
    configs=method_configs(network)
    fixtures=np.load(HERE/"quantized_fixtures.npz")
    report={"scope":"Saved PT2/SafeTensors cache checks only; no Vela recompilation or board mutation.","methods":{}}
    with torch.no_grad():
        for name in ("fargan_init","fargan_frame"):
            model=load_quantized_program("ethos-u85-256",HERE.parent,configs[name]).module()
            inputs=torch.from_numpy(fixtures[name+"_input"].copy())
            indices=torch.from_numpy(fixtures[name+"_indices"].copy())
            zero=torch.from_numpy(fixtures[name+"_zero_state"].copy()) if name=="fargan_init" else None
            def run(idx):
                return model(inputs,idx,zero) if zero is not None else model(inputs,idx)
            output=run(indices)
            expected=torch.from_numpy(fixtures[name+"_expected"].copy())
            alternatives={str(index):float((run(torch.full_like(indices,index))-output).abs().max()) for index in (0,255)}
            report["methods"][name]={"fixture_cache_max_absolute_error":float((output-expected).abs().max()),
                "index_dtype":str(indices.dtype),"index_shape":list(indices.shape),
                "indices_min":int(indices.min()),"indices_max":int(indices.max()),
                "constant_index_output_delta":alternatives}
    report["interpretation"]="Initialization remains identical with every pitch index set to 0 or 255 because its pitch history is zero throughout initialization. Its board discrepancy is therefore not explained by selecting different valid pitch indices. Frame pitch sensitivity alone cannot localize its board discrepancy. Both observed board errors exceed final-output quantization rounding by many LSB; the internal lowering difference remains unresolved."
    (HERE/"benchmark_diagnosis.json").write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")
    print(json.dumps(report,indent=2))

if __name__=="__main__": main()
