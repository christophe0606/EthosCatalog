# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Report learned-logit distribution changes separately from sampled paths."""
import argparse,json
from pathlib import Path
import numpy as np
import torch
import torchao.quantization.pt2e.quantize_pt2e
import executorch.exir.passes.dim_order_ops_registry  # Arm-inserted casts
from model import load_model
from quantize import samples


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--tag',required=True);args=parser.parse_args()
    here=Path(__file__).resolve().parent;torch.set_num_threads(1)
    reference=load_model();quantized=torch.export.load(here/f'{args.tag}_quantized.pt2').module(check_guards=False)
    records=[];fixtures={}
    with torch.no_grad():
        for inst,case in enumerate(samples(711)):
            actual=quantized(*case);expected=reference(*case)
            lp=expected[3].log_softmax(-1);lq=actual[3].log_softmax(-1);p=lp.exp();q=lq.exp()
            records.append({'instrument_id':inst,
                'max_errors_raw_pitch_expression_logits':[float((a-b).abs().max()) for a,b in zip(actual,expected)],
                'mean_logits_kl_float_to_ptq':float((p*(lp-lq)).sum(-1).mean()),
                'first_frame_logits_kl':float((p*(lp-lq)).sum(-1)[:,0].mean()),
                'mean_logits_total_variation':float((p-q).abs().sum(-1).mean()/2),
                'first_frame_logits_mae':float((actual[3][:,0]-expected[3][:,0]).abs().mean()),
                'pitch_rmse_cents':float(((actual[1]-expected[1])**2).mean().sqrt()*100),
                'raw_controls_rmse':float(((actual[0]-expected[0])**2).mean().sqrt())})
            for i,x in enumerate(case):fixtures[f'{inst}_input_{i}']=x.numpy()
            for i,x in enumerate(actual):fixtures[f'{inst}_ptq_{i}']=x.numpy()
            for i,x in enumerate(expected):fixtures[f'{inst}_float_{i}']=x.numpy()
    report={'cases':records,'scope':'Complete four-frame graph; probabilities before nucleus cutoff; independently seeded quantiles; excludes waveform and long-run PTQ quality'}
    (here/f'ptq_{args.tag}_distribution.json').write_text(json.dumps(report,indent=2)+'\n')
    np.savez_compressed(here/f'ptq_{args.tag}_fixtures.npz',**fixtures)
    print(json.dumps(records,indent=2),flush=True)


if __name__=='__main__':main()
