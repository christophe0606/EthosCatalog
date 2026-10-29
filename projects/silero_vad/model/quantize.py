#!/usr/bin/env python3
# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Silero VAD PTQ: first half upstream WAV calibration, second half evaluation."""
import argparse,json,sys,wave
import yaml
from pathlib import Path
import numpy as np
import torch
ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'scripts'))
from project_config import DEFAULT_SOLUTION,selected_project
from quantized_model import compile_spec,save_quantized_program
from model import SileroVAD

def waveform():
    with wave.open(str(Path(__file__).parent/'upstream/test.wav')) as f:
        assert f.getframerate()==16000 and f.getnchannels()==1 and f.getsampwidth()==2
        return torch.from_numpy(np.frombuffer(f.readframes(f.getnframes()),dtype='<i2').copy()).float()/32768

def frames(audio):
    context=torch.zeros(64)
    for start in range(0,len(audio)-511,512):
        chunk=audio[start:start+512]
        yield torch.cat((context,chunk)).reshape(1,576)
        context=chunk[-64:]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--solution',type=Path,default=DEFAULT_SOLUTION)
    parser.add_argument('--project',default='silero_vad')
    args=parser.parse_args()
    _,solution,project,_,network=selected_project(args.solution,args.project)
    assert project.parent==Path(__file__).resolve().parents[1]
    torch.set_num_threads(1)
    from executorch.backends.arm.quantizer import EthosUQuantizer,get_symmetric_a16w8_quantization_config
    from torchao.quantization.pt2e.quantize_pt2e import prepare_pt2e,convert_pt2e
    spec=compile_spec(solution,project.parent,network)
    model=SileroVAD().eval(); source=torch.jit.load(str(Path(__file__).parent/'upstream/silero_vad.jit')).eval()
    audio=waveform(); split=len(audio)//2
    samples=list(frames(audio[:split])); example=(samples[0],torch.zeros(1,128),torch.zeros(1,128))
    graph=torch.export.export(model,example).module(check_guards=False)
    quantizer=EthosUQuantizer(spec).set_global(get_symmetric_a16w8_quantization_config(epsilon=2**-24))
    prepared=prepare_pt2e(graph,quantizer)
    h=torch.zeros(1,128);c=h.clone()
    parity=0.0
    with torch.no_grad():
        for x in samples:
            out=model(x,h,c);ref,state=source._model(x,torch.stack((h,c)))
            parity=max(parity,float((out[0]-ref).abs().max()),float((out[1]-state[0]).abs().max()),float((out[2]-state[1]).abs().max()))
            prepared(x,h,c);_,h,c=out
        # Calibrate zero/signed tones and noise to cover startup and PCM range.
        for amplitude in (0.,.001,.01,.1,.5):
            x=torch.sin(torch.arange(576)*(.08))*amplitude
            prepared(x.reshape(1,576),torch.zeros(1,128),torch.zeros(1,128))
    quantized=convert_pt2e(prepared,fold_quantize=True)
    h=torch.zeros(1,128);c=h.clone();qh=h.clone();qc=h.clone();errors=[]; decisions=0;positive=0
    with torch.no_grad():
        for x in frames(audio[split:]):
            p,h,c=model(x,h,c);q,qh,qc=quantized(x,qh,qc)
            errors.append(abs(p.item()-q.item()));decisions+=int((p.item()>=.5)!=(q.item()>=.5));positive+=int(p.item()>=.5)
    report={'reference':'official pinned TorchScript 16k branch','float_probability_and_state_max_absolute_error':parity,'calibration_frames':len(samples),'evaluation_frames':len(errors),'evaluation_max_probability_absolute_error':max(errors),'evaluation_mean_probability_absolute_error':sum(errors)/len(errors),'threshold':.5,'decision_disagreements':decisions,'reference_positive_frames':positive,'activation':'int16','weights':'int8','data':'upstream tests/data/test.wav first30s calibration last30s evaluation; continuous recurrent state independently propagated'}
    assert parity<1e-5,report
    print(json.dumps(report,indent=2),flush=True)
    (Path(__file__).parent/'conversion_metrics.json').write_text(json.dumps(report,indent=2)+'\n')
    config=project.parent/'model/network.yml'
    document=yaml.safe_load(config.read_text())
    document['network']['quantization']['error']=report
    config.write_text('# Copyright 2026 Arm Limited and/or its affiliates.\n# SPDX-License-Identifier: Apache-2.0\n'+yaml.safe_dump(document,sort_keys=False))
    program=torch.export.export(quantized,example)
    save_quantized_program(program,spec.target,project.parent,network)

if __name__=='__main__':main()
