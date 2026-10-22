# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Independent NumPy implementation of upstream equations for conversion checks.

The reference reads original checkpoint arrays directly and never imports or
calls the PyTorch model for its arithmetic. TensorFlow runtime is not required.
"""
from pathlib import Path
import json
import numpy as np
import torch
from checkpoint_reader import read_checkpoint
from model import load_models,zero_state,step,encode,distribution

PREFIX='rnn/LSTMAttentionCell/'

def sigmoid(x): return 1/(1+np.exp(-x))

def numpy_step(tensors,state,xy,chars):
    def lstm(prefix,x,c,h):
        gates=np.concatenate([x,h],1)@tensors[prefix+'/kernel']+tensors[prefix+'/bias']
        i,j,f,o=np.split(gates,4,axis=1)
        c=c*sigmoid(f+1)+sigmoid(i)*np.tanh(j)
        return np.tanh(c)*sigmoid(o),c
    h1,c1=lstm(PREFIX+'lstm_cell',np.concatenate([state['w'],xy],1),state['c1'],state['h1'])
    a=np.concatenate([state['w'],xy,h1],1)@tensors[PREFIX+'attention/weights']+tensors[PREFIX+'attention/biases']
    alpha,beta,delta=np.split(np.logaddexp(a,0),3,axis=1)
    beta=np.maximum(beta,.01); kappa=state['kappa']+delta/25
    phi=(alpha[:,:,None]*np.exp(-(kappa[:,:,None]-np.arange(len(chars),dtype=np.float32))**2/beta[:,:,None])).sum(axis=1)
    onehot=np.eye(73,dtype=np.float32)[chars]
    w=phi@onehot
    h2,c2=lstm(PREFIX+'lstm_cell_1',np.concatenate([xy,h1,w],1),state['c2'],state['h2'])
    h3,c3=lstm(PREFIX+'lstm_cell_2',np.concatenate([xy,h2,w],1),state['c3'],state['h3'])
    logits=h3@tensors['rnn/gmm/weights']+tensors['rnn/gmm/biases']
    return dict(h1=h1,c1=c1,h2=h2,c2=c2,h3=h3,c3=c3,w=w,kappa=kappa),logits,phi

@torch.no_grad()
def main():
    torch.set_num_threads(1)
    directory=Path(__file__).parent
    tensors=read_checkpoint(directory/'original/checkpoints/model-17900')
    models=load_models(); np_state={k:v.numpy().copy() for k,v in zero_state().items()}; pt_state=zero_state()
    chars=encode('Independent numerical reference.')
    rng=np.random.default_rng(191); maximum={}; decoder_max=0
    for i in range(300):
        # Forced, identical state-independent strokes prevent stochastic drift.
        xy=rng.normal(0,.4,(1,3)).astype(np.float32); xy[0,2]=int(i%17==0)
        np_state,np_logits,np_phi=numpy_step(tensors,np_state,xy,chars)
        pt_state,pt_logits,pt_phi=step(models,pt_state,torch.from_numpy(xy),chars)
        for name in np_state:
            maximum[name]=max(maximum.get(name,0),float(abs(np_state[name]-pt_state[name].numpy()).max()))
        maximum['gmm']=max(maximum.get('gmm',0),float(abs(np_logits-pt_logits.numpy()).max()))
        maximum['phi']=max(maximum.get('phi',0),float(abs(np_phi-pt_phi.numpy()).max()))
        # Independently compute MDN parse including threshold/renormalization.
        z=np_logits.reshape(-1).astype(np.float64); p=np.exp(z[:20]*1.5-max(z[:20]*1.5)); p/=sum(p); p[p<.01]=0;p/=sum(p)
        sigma=np.maximum(np.exp(z[20:60]-.5),1e-4);rho=np.clip(np.tanh(z[60:80]),-1+1e-8,1-1e-8)
        pen=1/(1+np.exp(-z[120]));pen=0 if pen<.01 else np.clip(pen,1e-8,1-1e-8)
        for lhs,rhs in zip((p,sigma,rho,z[80:120],pen),distribution(pt_logits)):
            decoder_max=max(decoder_max,float(np.max(abs(lhs-rhs))))
    result={'reference':'Independent float32 NumPy equations using original TensorFlow checkpoint tensors; not original TensorFlow runtime',
            'forced_steps':300,'sentence':'Independent numerical reference.','seed':191,'max_absolute_error':maximum,
            'decoder_max_absolute_error':decoder_max,'parameter_count':sum(p.numel() for m in models.values() for p in m.parameters())}
    (directory/'float_equivalence.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))
    assert max(maximum.values())<.005

if __name__=='__main__': main()
