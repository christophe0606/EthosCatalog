# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""PTQ the complete fixed-score MIDI-DDSP graph before any manual partition."""
import argparse,json,sys,time,traceback
from pathlib import Path
import numpy as np
import torch
import yaml
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from project_config import DEFAULT_SOLUTION,selected_project
from quantized_model import compile_spec,save_quantized_program
from model import load_model


def samples(seed=1,ids=range(13)):
    generator=torch.Generator().manual_seed(seed)
    for inst in ids:
        notes=torch.tensor([[48+inst,60+inst]],dtype=torch.int64)
        duration=torch.tensor([[[.05],[.08]]])
        index=torch.tensor([[0,0,1,1]])
        f=torch.zeros((1,4,4));f[0,:,0]=notes[0,index[0]].float()/127.
        f[0,[0,2],1]=1;f[0,[1,3],2]=1;f[0,:,3]=torch.tensor([.5,1.,.5,1.])
        yield notes,duration,torch.tensor([inst]),index,f,torch.rand((1,4,1),generator=generator)*.98+.01


def quantize_model(spec,project_dir,network,tag=None):
    from executorch.backends.arm.quantizer import EthosUQuantizer,get_symmetric_a16w8_quantization_config
    from torchao.quantization.pt2e.quantize_pt2e import prepare_pt2e,convert_pt2e
    from torchao.quantization.pt2e.observer import MinMaxObserver
    from dataclasses import replace
    from model import TopPSample,Norm
    torch.set_num_threads(1);model=load_model();calibration=list(samples());example=calibration[0]
    print('Exporting complete expression+synthesis graph',flush=True)
    graph=torch.export.export(model,example).module(check_guards=False)
    quantizer=EthosUQuantizer(spec)
    config=get_symmetric_a16w8_quantization_config(epsilon=2**-24)
    activation=replace(config.input_activation,observer_or_fake_quant_ctr=MinMaxObserver.with_args(eps=2**-24))
    config=replace(config,input_activation=activation,output_activation=activation)
    quantizer.set_global(config)
    quantizer.set_module_type(TopPSample,None)
    quantizer.set_module_type(Norm,None)
    prepared=prepare_pt2e(graph,quantizer)
    print('Calibrating complete graph on all 13 IDs',flush=True)
    with torch.no_grad():
        for i,case in enumerate(calibration):
            prepared(*case);print('calibration instrument',i,flush=True)
        for observer in prepared.modules():
            if isinstance(observer,MinMaxObserver):
                observer.min_val.mul_(1.25);observer.max_val.mul_(1.25)
    converted=convert_pt2e(prepared,fold_quantize=True)
    program=torch.export.export(converted,example)
    if tag:
        network={**network,'graph':f'model/{tag}_quantized.pt2','safetensors':f'model/{tag}_quantized.safetensors'}
    save_quantized_program(program,spec.target,project_dir,network)
    errors=[];distribution=[];fixtures={}
    with torch.no_grad():
        for instrument_id,case in enumerate(samples(711)):
            actual=converted(*case);expected=model(*case)
            errors.append([float((a-b).abs().max()) for a,b in zip(actual,expected)])
            logp=expected[3].log_softmax(-1);logq=actual[3].log_softmax(-1)
            p=logp.exp();q=logq.exp()
            distribution.append({'instrument_id':instrument_id,
                'mean_logits_kl_float_to_ptq':float((p*(logp-logq)).sum(-1).mean()),
                'mean_logits_total_variation':float((p-q).abs().sum(-1).mean()/2),
                'logits_mae':float((actual[3]-expected[3]).abs().mean()),
                'pitch_rmse_cents':float(((actual[1]-expected[1])**2).mean().sqrt()*100),
                'raw_controls_rmse':float(((actual[0]-expected[0])**2).mean().sqrt())})
            for i,value in enumerate(actual):fixtures[f'{instrument_id}_ptq_{i}']=value.numpy()
            for i,value in enumerate(expected):fixtures[f'{instrument_id}_float_{i}']=value.numpy()
    metrics={'scheme':'symmetric a16w8 learned operators; float normalization and categorical sampler; MinMax with25%guard; no retraining',
             'calibration':'13 synthetic two-note four-frame cases covering all 13 IDs',
             'evaluation':'13 independently seeded categorical quantiles; same notes',
             'max_abs_error_by_instrument_raw_pitch_expression_logits':errors,
             'distribution_metrics':distribution,
             'limitations':'Tiny complete-score export establishes partitioning evidence, not final musical quality or real-time capacity'}
    (HERE/f'ptq_{tag or "complete"}_validation.json').write_text(json.dumps(metrics,indent=2)+'\n')
    np.savez_compressed(HERE/f'ptq_{tag or "complete"}_fixtures.npz',**fixtures)
    if not tag:
        path=HERE/'network.yml';config=yaml.safe_load(path.read_text());config['network']['quantization']['error']=metrics
        path.write_text(yaml.safe_dump(config,sort_keys=False))
    print('Complete quantized graph saved',flush=True)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--solution',type=Path,default=DEFAULT_SOLUTION);parser.add_argument('--project',default='midi_ddsp')
    parser.add_argument('--tag',help='Diagnostic cache tag; do not update network.yml')
    args=parser.parse_args();_,solution,project,_,network=selected_project(args.solution,args.project)
    quantize_model(compile_spec(solution,project.parent,network),project.parent,network,args.tag)


if __name__=='__main__':main()
