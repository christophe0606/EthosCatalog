# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""PTQ reusable MIDI-DDSP methods using captured full-window float trajectories."""
import argparse,json,sys
from pathlib import Path
from dataclasses import replace
import numpy as np
import torch,yaml
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from project_config import DEFAULT_SOLUTION,selected_project
from quantized_model import compile_spec,save_quantized_program,method_configs
from model import load_model,Norm
from deployment import methods,render_controls,score,FRAMES,SHAPES


def metric(actual,expected):
    error=(actual-expected).float()
    return {'max_absolute_error':float(error.abs().max()),'mae':float(error.abs().mean()),
            'rmse':float(error.square().mean().sqrt()),'reference_rms':float(expected.square().mean().sqrt())}


def quantize_model(spec,project_dir,network):
    from executorch.backends.arm.quantizer import EthosUQuantizer,get_symmetric_a16w8_quantization_config
    from torchao.quantization.pt2e.quantize_pt2e import prepare_pt2e,convert_pt2e
    from torchao.quantization.pt2e.observer import MinMaxObserver
    torch.set_num_threads(1);model=load_model();ops=methods(model)
    captured={k:[] for k in ops};parity=[]
    with torch.no_grad():
        for inst in range(13):
            inputs=score(inst,FRAMES,1100+inst)
            before={k:len(v) for k,v in captured.items()}
            actual=render_controls(model,ops,*inputs,capture=captured)
            # Full original composed model uses identical full-window math.
            expected=model(*inputs)
            errors=[metric(a,b) for a,b in zip(actual,expected)]
            if max(m['max_absolute_error'] for m in errors)>0.0001:raise RuntimeError('Method schedule differs from full float model')
            parity.append({'instrument_id':inst,'metrics_raw_pitch_expression_logits':errors})
            # Retain up to24 well-spaced recurrent/dense inputs per instrument,
            # plus every full harmonic block; this bounds host calibration RAM.
            for name,values in captured.items():
                start=before[name];new=values[start:]
                if len(new)>24:
                    selected=np.linspace(0,len(new)-1,24).astype(int)
                    values[start:]=[new[i] for i in selected]
            print('Captured full1000frame float trajectory instrument',inst,flush=True)
    (HERE/'deployment_float_parity.json').write_text(json.dumps({'frames':FRAMES,'cases':parity},indent=2)+'\n')
    config=get_symmetric_a16w8_quantization_config(epsilon=2**-24)
    activation=replace(config.input_activation,observer_or_fake_quant_ctr=MinMaxObserver.with_args(eps=2**-24))
    config=replace(config,input_activation=activation,output_activation=activation)
    quantized={};metrics={};fixtures={}
    settings=method_configs(network)
    for name,op in ops.items():
        print('Preparing method',name,'calibration',len(captured[name]),flush=True)
        examples=captured[name];example=(examples[0],)
        graph=torch.export.export(op,example).module(check_guards=False)
        quantizer=EthosUQuantizer(spec);quantizer.set_global(config);quantizer.set_module_type(Norm,None)
        prepared=prepare_pt2e(graph,quantizer)
        with torch.no_grad():
            for x in examples:prepared(x)
            for observer in prepared.modules():
                if isinstance(observer,MinMaxObserver):observer.min_val.mul_(1.25);observer.max_val.mul_(1.25)
        converted=convert_pt2e(prepared,fold_quantize=True)
        exported=torch.export.export(converted,example)
        save_quantized_program(exported,spec.target,project_dir,settings[name])
        quantized[name]=converted
        with torch.no_grad():
            x=examples[len(examples)//2];expected=op(x);actual=converted(x)
        metrics[name]={**metric(actual,expected),'calibration_samples':len(examples),
                       'input_shape':list(x.shape),'output_shape':list(actual.shape)}
        fixtures[name+'_input']=x.numpy();fixtures[name+'_expected']=actual.numpy()
        print(name,metrics[name],flush=True)
    closed_loop=[]
    with torch.no_grad():
        for inst in range(13):
            inputs=score(inst,FRAMES,9100+inst)
            actual=render_controls(model,quantized,*inputs)
            expected=render_controls(model,ops,*inputs)
            lp=expected[3].log_softmax(-1);lq=actual[3].log_softmax(-1)
            closed_loop.append({'instrument_id':inst,'frames':FRAMES,
                'metrics_raw_pitch_expression_logits':[metric(a,b) for a,b in zip(actual,expected)],
                'mean_logits_kl_float_to_ptq':float((lp.exp()*(lp-lq)).sum(-1).mean()),
                'mean_logits_total_variation':float((lp.exp()-lq.exp()).abs().sum(-1).mean()/2),
                'finite':all(bool(torch.isfinite(x).all()) for x in actual)})
            print('Closed-loop1000frame instrument',inst,closed_loop[-1],flush=True)
    report={'preprocessing':'Upstream clipped synthesis conditioning; raw masked expression feedback; zero relative positions on rests; four score regions (<100)',
            'scheme':'a16w8 learned linear/conv/GRU ops; exact float normalization and CPU top-p; no training',
            'calibration':'13 synthetic complete1000frame trajectories, up to24 sampled inputs per shared cell/method per instrument plus13 full harmonic blocks; MinMax25%guard',
            'reference':'Imported original checkpoints; independent TensorFlow parity in float_validation.json; split/full float equality in deployment_float_parity.json',
            'methods':metrics,'closed_loop':closed_loop,
            'limitations':'Synthetic score patterns only; stochastic trajectories may differ after PTQ; no final waveform perceptual gate or board timing yet'}
    (HERE/'deployment_ptq_validation.json').write_text(json.dumps(report,indent=2)+'\n')
    np.savez_compressed(HERE/'deployment_fixtures.npz',**fixtures)
    header=['// Copyright 2026 Arm Limited and/or its affiliates.','// SPDX-License-Identifier: Apache-2.0','#pragma once']
    manifest={}
    for name in ops:
        x=fixtures[name+'_input'].flatten();y=fixtures[name+'_expected'].flatten()
        # Only small methods go into MRAM fixtures. Harmonic data remains in
        # NPZ until firmware fixture/asset layout is selected by benchmarking.
        manifest[name]={'input_count':len(x),'output_count':len(y),'fixture_in_header':name!='harmonic'}
        if name=='harmonic':continue
        for suffix,data in [('input',x),('expected',y)]:
            header.append(f'static const float midi_{name}_{suffix}[{len(data)}] = {{'+','.join(f'{float(v):.9e}f' for v in data)+'};')
    (HERE/'model_test_data.h').write_text('\n'.join(header)+'\n')
    (HERE/'method_contract.json').write_text(json.dumps({'frames':FRAMES,'methods':manifest,'shapes':{k:[list(v[0].shape),list(quantized[k](v[0]).shape)] for k,v in captured.items()}},indent=2)+'\n')
    path=HERE/'network.yml';config=yaml.safe_load(path.read_text());config['network']['quantization']['error']=report
    path.write_text(yaml.safe_dump(config,sort_keys=False))


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--solution',type=Path,default=DEFAULT_SOLUTION);parser.add_argument('--project',default='midi_ddsp')
    args=parser.parse_args();_,solution,project,_,network=selected_project(args.solution,args.project)
    quantize_model(compile_spec(solution,project.parent,network),project.parent,network)


if __name__=='__main__':main()
