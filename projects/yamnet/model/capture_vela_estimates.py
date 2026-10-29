#!/usr/bin/env python3
# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Export compiler estimates and prove their command/weights equal the board PTE.

Does not replace the active ai_layer. Temporary compile metadata changes the PTE
container hash; the executable command stream and weights must remain identical.
"""
import hashlib
import csv
import json
import os
from pathlib import Path
import struct
import sys
import numpy as np
import yaml

ROOT=Path(__file__).resolve().parents[3]
MODEL=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT/'scripts'))
from quantized_model import compile_spec, load_quantized_program, sha256
from create_ai_layer import export_model
from validate_networks import load_network


def main():
    work=ROOT/'tmp/yamnet_vela';work.mkdir(parents=True,exist_ok=True)
    os.environ['TEMP']=str(ROOT/'tmp');os.environ['TMP']=str(ROOT/'tmp')
    network=load_network(MODEL/'network.yml')
    solution=yaml.safe_load((ROOT/'EthosCatalog.csolution.yml').read_text())['solution']
    program=load_quantized_program('ethos-u85-256',MODEL.parent,network)
    data=(MODEL.parent/'ai_layer/model.pte').read_bytes()
    # Give each automatically generated delegate its own retained compiler CSV.
    # This wraps the public compiler call only for evidence collection; it does
    # not affect partitioning, compiler flags or the installed package files.
    import executorch.backends.arm.ethosu.backend as backend
    original_compile=backend.vela_compile
    blocks=[];rows=[]
    def capture(flatbuffer,args,verbose=False,intermediate_path=None):
        index=len(blocks);folder=work/f'delegate_{index:02d}';folder.mkdir(exist_ok=True)
        binary=original_compile(flatbuffer,list(args),verbose=verbose,intermediate_path=str(folder))
        offset=data.find(binary)
        assert offset>=0, f'delegate {index} executable stream differs from active board PTE'
        archive=np.load(folder/'output/out_vela.npz',allow_pickle=False)
        streams={}
        for name in ['cmd_data','weight_data']:
            payload=archive[name].tobytes()
            streams[name]={'bytes':len(payload),'sha256':hashlib.sha256(payload).hexdigest()}
        blocks.append({'delegate':index,'pte_offset':offset,'stream_bytes':len(binary),
                       'stream_sha256':hashlib.sha256(binary).hexdigest(),'identical':True,'blocks':streams})
        summaries=list(folder.glob('output/*summary_'+network['vela']['system_config']+'.csv'))
        assert len(summaries)==1
        with summaries[0].open(newline='') as source:
            rows.extend({'delegate':index,**row} for row in csv.DictReader(source))
        return binary
    backend.vela_compile=capture
    try:
        pte,delegates=export_model(program,compile_spec(solution,MODEL.parent,network))
    finally:
        backend.vela_compile=original_compile
    assert delegates==len(blocks) and len({b['pte_offset'] for b in blocks})==delegates
    with (MODEL/'benchmark_vela_summary.csv').open('w',newline='') as output:
        writer=csv.DictWriter(output,fieldnames=list(rows[0]));writer.writeheader();writer.writerows(rows)
    report={'board_pte_sha256':sha256(MODEL.parent/'ai_layer/model.pte'),
            'estimate_container_sha256':hashlib.sha256(pte).hexdigest(),
            'estimate_container_bytes':len(pte),'summary_sha256':sha256(MODEL/'benchmark_vela_summary.csv'),
            'delegates':delegates,'blocks':blocks,
            'total_estimated_cycles':sum(int(float(row['cycles_total'])) for row in rows),
            'scope':'Compiler estimates; every delegate stream identical to active board PTE; CPU work excluded'}
    (MODEL/'benchmark_vela_provenance.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))


if __name__=='__main__':
    main()

