#!/usr/bin/env python3
# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Convert checkpoint, calibrate a16w8 dense projections and measure PTQ."""
import argparse
import json
from pathlib import Path
import sys
import numpy as np
import torch
import yaml
ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'scripts'))
from project_config import DEFAULT_SOLUTION,selected_project
from quantized_model import compile_spec,save_quantized_program,method_configs
from model import SHAPES,load_models,generate

CALIBRATION=['Hello world','The quick brown fox jumps over the lazy dog.',
             'Handwriting on Arm','0123456789 !?','A little pen writes white on black.',
             'Now try another sentence','Welcome to the demo','OpenAI and Ethos']
EVALUATION=['This is a test.','Animated handwriting','A new sentence replaces the old one.']

def plot_strokes(path, outputs):
    """Pure SVG avoids requiring the original plotting/training dependencies."""
    rows=[]
    for row,(label,stroke) in enumerate(outputs):
        coords=np.cumsum(stroke[:,:2],axis=0); lo=coords.min(0); hi=coords.max(0)
        scale=min(900/max(hi[0]-lo[0],1),90/max(hi[1]-lo[1],1))
        coords=(coords-lo)*scale; coords[:,1]=100-coords[:,1]+row*140
        prev=1; commands=[]
        for (x,y),pen in zip(coords,stroke[:,2]):
            commands.append(f'{"M" if prev else "L"}{x+20:.2f},{y:.2f}'); prev=pen
        rows.append(f'<text x="20" y="{row*140+125}" fill="white">{label}</text><path d="{" ".join(commands)}" fill="none" stroke="white" stroke-width="1.5"/>')
    path.write_text(f'<svg xmlns="http://www.w3.org/2000/svg" width="960" height="{len(outputs)*140}"><rect width="100%" height="100%" fill="black"/>{"".join(rows)}</svg>')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--solution',type=Path,default=DEFAULT_SOLUTION)
    parser.add_argument('--project')
    args=parser.parse_args()
    _,solution,project,config,network=selected_project(args.solution,args.project)
    if project.parent!=Path(__file__).resolve().parents[1]:
        parser.error('This quantizer owns handwriting_synthesis only')
    torch.set_num_threads(1); torch.manual_seed(17)
    directory=project.parent/'model'; models=load_models()
    from safetensors.torch import save_file
    save_file({f'{name}.{key}':value.detach().contiguous() for name,m in models.items() for key,value in m.state_dict().items()},directory/'handwriting_float.safetensors',
              metadata={'original_revision':'5f58984c3bc793bd8930bad7012cd2008d4a4a66','upstream_license':'No stated reuse license'})
    calibration={name:[] for name in SHAPES}; plots=[]
    for index,sentence in enumerate(CALIBRATION):
        strokes,_=generate(models,sentence,seed=101+index,capture=calibration)
        plots.append((f'Float calibration: {sentence}',strokes)); print('generated',sentence,len(strokes),flush=True)
    heldout={name:[] for name in SHAPES}
    for index,sentence in enumerate(EVALUATION):
        strokes,_=generate(models,sentence,seed=801+index,capture=heldout)
        plots.append((f'Float held out: {sentence}',strokes))
    from executorch.backends.arm.quantizer import EthosUQuantizer,get_symmetric_a16w8_quantization_config
    from torchao.quantization.pt2e.quantize_pt2e import convert_pt2e,prepare_pt2e
    from torchao.quantization.pt2e.observer import MinMaxObserver
    from dataclasses import replace
    spec=compile_spec(solution,project.parent,network)
    quantized_models={}; metrics={}; fixtures={}
    for name,model in models.items():
        samples=calibration[name]; example=(samples[0],)
        graph=torch.export.export(model,example).module(check_guards=False)
        q=EthosUQuantizer(spec)
        qconfig=get_symmetric_a16w8_quantization_config(epsilon=2**-24)
        activation=replace(qconfig.input_activation,observer_or_fake_quant_ctr=MinMaxObserver.with_args(eps=2**-24))
        qconfig=replace(qconfig,input_activation=activation,output_activation=activation)
        q.set_global(qconfig)
        prepared=prepare_pt2e(graph,q)
        with torch.no_grad():
            for start in range(0,len(samples),128):
                prepared(torch.cat(samples[start:start+128],dim=0))
            # Rare autoregressive trajectories can exceed the calibration set.
            # Preserve a 50% activation guard rather than histogram clipping.
            for observer in prepared.modules():
                if isinstance(observer,MinMaxObserver):
                    observer.min_val.mul_(1.5); observer.max_val.mul_(1.5)
        converted=convert_pt2e(prepared,fold_quantize=True)
        program=torch.export.export(converted,example)
        save_quantized_program(program,spec.target,project.parent,method_configs(network)[name])
        quantized_models[name]=converted
        errors=[]; references=[]
        with torch.no_grad():
            for x in heldout[name][::3]:
                reference=model(x); errors.append((converted(x)-reference).numpy()); references.append(reference.numpy())
        error=np.concatenate(errors); reference=np.concatenate(references)
        metrics[name]={'calibration_samples':len(samples),'evaluation_samples':len(errors),
                       'max_absolute_error':float(abs(error).max()),'mean_absolute_error':float(abs(error).mean()),
                       'root_mean_square_error':float(np.sqrt(np.mean(error**2))),
                       'normalized_rmse_percent':float(100*np.sqrt(np.mean(error**2))/np.sqrt(np.mean(reference**2)))}
        activation_nodes=[n for n in program.graph.nodes if n.target==torch.ops.quantized_decomposed.quantize_per_tensor.default]
        metrics[name]['input_quantization_scale']=float(activation_nodes[0].args[1])
        metrics[name]['output_quantization_scale']=float(activation_nodes[-1].args[1])
        fixture_x=heldout[name][min(100,len(heldout[name])-1)]
        fixtures[name]=(fixture_x.numpy().reshape(-1),converted(fixture_x).detach().numpy().reshape(-1))
        print(name,metrics[name],flush=True)
    sequences=[]
    for index,sentence in enumerate(EVALUATION):
        float_strokes,float_state=generate(models,sentence,seed=801+index)
        quant_strokes,quant_state=generate(quantized_models,sentence,seed=801+index)
        plots.append((f'a16w8 held out: {sentence}',quant_strokes))
        sequences.append({'sentence':sentence,'seed':801+index,'float_points':len(float_strokes),'quantized_points':len(quant_strokes),
                          'float_final_kappa_mean':float(float_state['kappa'].mean()),'quantized_final_kappa_mean':float(quant_state['kappa'].mean()),
                          'finite':bool(np.isfinite(quant_strokes).all())})
        np.savez(directory/f'sample_{index}.npz',float=float_strokes,quantized=quant_strokes)
    plot_strokes(directory/'conversion_samples.svg',plots)
    header=['// Copyright 2026 Arm Limited and/or its affiliates.','// SPDX-License-Identifier: Apache-2.0',
            '// Generated by model/quantize.py. Deterministic held-out dense-method fixtures.','#pragma once']
    for name,(x,y) in fixtures.items():
        scale=metrics[name]['output_quantization_scale']
        header.append(f'#define HANDWRITING_{name.upper()}_OUTPUT_SCALE {scale:.9e}f')
        header.append(f'#define HANDWRITING_{name.upper()}_BOARD_ATOL {4*scale:.9e}f')
        for suffix,data in [('input',x),('expected',y)]:
            values=','.join(f'{float(value):.9e}f' for value in data)
            header.append(f'static const float handwriting_{name}_{suffix}[{len(data)}] = {{{values}}};')
    (directory/'model_test_data.h').write_text('\n'.join(header)+'\n')
    result={'scheme':'post-training symmetric a16w8, per-channel int8 weights/int16 activations',
            'observer':'Full MinMax over all float trajectory steps, symmetric range with 50% activation guard',
            'reference':'Original pretrained checkpoint mapped to float PyTorch; no training',
            'calibration_sentences':CALIBRATION,'evaluation_sentences':EVALUATION,'dense_methods':metrics,'closed_loop_sequences':sequences,
            'limitations':'Synthetic text/float-generated traces; no IAM held-out ground-truth recognition evaluation. Stochastic paths diverge after PTQ.'}
    (directory/'conversion_metrics.json').write_text(json.dumps(result,indent=2)+'\n')
    content=yaml.safe_load(config.read_text()); content['network']['quantization']['error']=result
    config.write_text(yaml.safe_dump(content,sort_keys=False))

if __name__=='__main__': main()
