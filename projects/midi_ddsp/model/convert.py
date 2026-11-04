# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Import only inference tensors from original TensorFlow checkpoints."""
import argparse
import hashlib
import json
from pathlib import Path
import numpy as np

HERE=Path(__file__).resolve().parent


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--checkpoint-dir',type=Path,required=True)
    parser.add_argument('--export-only',action='store_true',help='Re-export existing imported weights without replacing the safetensors file')
    args=parser.parse_args()
    if args.export_only:
        export_example()
        return
    import tensorflow as tf
    import torch
    from safetensors.torch import save_file
    tensors={};counts={}
    for prefix,name,step in [('e','expression_generator','5000'),('s','synthesis_generator','50000')]:
        checkpoint=args.checkpoint_dir/name/step
        reader=tf.train.load_checkpoint(str(checkpoint))
        selected=[]
        for key,shape in tf.train.list_variables(str(checkpoint)):
            if not key.endswith('/.ATTRIBUTES/VARIABLE_VALUE'):continue
            if prefix=='s' and not key.startswith(('midi_decoder/','reverb_module/')):continue
            target=prefix+'/'+key.removesuffix('/.ATTRIBUTES/VARIABLE_VALUE')
            tensors[target]=torch.from_numpy(np.array(reader.get_tensor(key),copy=True))
            selected.append({'name':target,'shape':shape})
        counts[name]=selected
    save_file(tensors,HERE/'pretrained.safetensors',metadata={
        'source':'https://github.com/magenta/midi-ddsp/tree/models',
        'archive_sha256':'46bae44c16c3399c24f8f0ba2557fc0432bc12aa23088606d465b22fec2cd05e',
        'training':'none; original checkpoint imported without retraining'})
    report={'tensors':counts,'float_parameter_bytes':sum(v.numel()*v.element_size() for v in tensors.values()),
            'reference_source_sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in (HERE/'reference').iterdir() if p.is_file()}}
    (HERE/'import_report.json').write_text(json.dumps(report,indent=2)+'\n')
    print('Imported',len(tensors),'inference tensors;',report['float_parameter_bytes'],'bytes',flush=True)
    export_example()


def export_example():
    import torch
    torch.set_num_threads(1)
    from model import load_model
    m=load_model()
    notes=torch.tensor([[60,64]],dtype=torch.int64);duration=torch.tensor([[[.02],[.02]]])
    instrument=torch.tensor([0]);mapping=torch.tensor([[0,0,1,1]])
    features=torch.tensor([[[60/127,1,0,.5],[60/127,0,1,1.],[64/127,1,0,.5],[64/127,0,1,1.]]])
    uniform=torch.tensor([[[.2],[.4],[.6],[.8]]])
    inputs=(notes,duration,instrument,mapping,features,uniform)
    with torch.no_grad():out=m(*inputs)
    print('Complete model output shapes:',[list(x.shape) for x in out],flush=True)
    p=torch.export.export(m,inputs)
    torch.export.save(p,HERE/'complete_float.pt2')
    print('Complete float graph exported:',len(list(p.graph.nodes)),'nodes',flush=True)
    np.savez(HERE/'float_example.npz',**{f'input_{i}':x.numpy() for i,x in enumerate(inputs)},**{f'output_{i}':x.numpy() for i,x in enumerate(out)})


if __name__=='__main__':main()
