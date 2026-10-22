# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Converted pretrained model and CPU orchestration; see THIRD_PARTY_NOTICES.md.

Original model: Sean Vasquez, sjvasquez/handwriting-synthesis revision
5f58984c3bc793bd8930bad7012cd2008d4a4a66. No stated upstream reuse license.
The header licenses only new project-owned conversion code, not original work.
"""
from pathlib import Path
import numpy as np
import torch
from torch import nn
from torch.nn import functional as F

ALPHABET = '\x00 !"#\'(),-.0123456789:;?ABCDEFGHIJKLMNOPRSTUVWYabcdefghijklmnopqrstuvwxyz'
assert len(ALPHABET) == 73
HIDDEN = 400
SHAPES = {'lstm1': (476, 1600), 'attention': (476, 30),
          'lstm2': (876, 1600), 'lstm3': (876, 1600), 'gmm': (400, 121)}
PREFIXES = {'lstm1': 'rnn/LSTMAttentionCell/lstm_cell',
            'lstm2': 'rnn/LSTMAttentionCell/lstm_cell_1',
            'lstm3': 'rnn/LSTMAttentionCell/lstm_cell_2',
            'attention': 'rnn/LSTMAttentionCell/attention', 'gmm': 'rnn/gmm'}

class Dense(nn.Module):
    def __init__(self, inputs, outputs):
        super().__init__()
        self.linear = nn.Linear(inputs, outputs)

    def forward(self, x):
        return self.linear(x)

def load_models():
    from checkpoint_reader import read_checkpoint
    tensors = read_checkpoint(Path(__file__).parent/'original/checkpoints/model-17900')
    models = {}
    for name, (inputs, outputs) in SHAPES.items():
        model = Dense(inputs, outputs)
        prefix = PREFIXES[name]
        weight, bias = ('kernel', 'bias') if name.startswith('lstm') else ('weights', 'biases')
        with torch.no_grad():
            model.linear.weight.copy_(torch.from_numpy(tensors[prefix+'/'+weight].T.copy()))
            model.linear.bias.copy_(torch.from_numpy(tensors[prefix+'/'+bias]))
        models[name] = model.eval()
    return models

def encode(sentence):
    if not sentence or len(sentence) > 75 or any(c not in ALPHABET[1:] for c in sentence):
        raise ValueError('Expected 1..75 supported characters; see ALPHABET')
    return np.array([ALPHABET.index(c) for c in sentence] + [0], dtype=np.int64)

def zero_state():
    return {**{k: torch.zeros(1, 400) for k in ['h1','c1','h2','c2','h3','c3']},
            'w': torch.zeros(1,73), 'kappa': torch.zeros(1,10)}

def cell(gates, previous_c):
    # TensorFlow contrib LSTM defaults: i,j,f,o gate order, forget_bias=1.
    i, j, f, o = gates.chunk(4, dim=-1)
    c = previous_c*torch.sigmoid(f+1) + torch.sigmoid(i)*torch.tanh(j)
    return torch.tanh(c)*torch.sigmoid(o), c

def step(models, state, xy, chars, capture=None):
    def run(name, x):
        if capture is not None:
            capture[name].append(x.detach().clone())
        return models[name](x)
    # LSTM1 sees OLD window and h1. Attention sees old window, xy, NEW h1.
    h1,c1 = cell(run('lstm1', torch.cat([state['w'],xy,state['h1']],1)),state['c1'])
    attention = F.softplus(run('attention',torch.cat([state['w'],xy,h1],1)))
    alpha,beta,delta = attention.chunk(3,dim=1)
    kappa=state['kappa']+delta/25
    beta=beta.clamp(min=.01)
    u=torch.arange(len(chars),dtype=torch.float32).reshape(1,1,-1)
    phi=(alpha[:,:,None]*torch.exp(-(kappa[:,:,None]-u).square()/beta[:,:,None])).sum(1)
    # chars includes null terminator; equivalent to masked one-hot sum.
    w=torch.zeros(1,73).scatter_add_(1,torch.as_tensor(chars).reshape(1,-1),phi)
    h2,c2=cell(run('lstm2',torch.cat([xy,h1,w,state['h2']],1)),state['c2'])
    h3,c3=cell(run('lstm3',torch.cat([xy,h2,w,state['h3']],1)),state['c3'])
    logits=run('gmm',h3)
    return dict(h1=h1,c1=c1,h2=h2,c2=c2,h3=h3,c3=c3,w=w,kappa=kappa),logits,phi

def distribution(logits, bias=.5):
    z=logits.detach().numpy().reshape(-1).astype(np.float64)
    p=z[:20]*(1+bias); p=np.exp(p-p.max()); p/=p.sum(); p[p<.01]=0
    # TF Categorical normalizes the thresholded, unnormalized probabilities.
    p/=p.sum()
    sigma=np.maximum(np.exp(z[20:60]-bias),1e-4)
    rho=np.clip(np.tanh(z[60:80]),-1+1e-8,1-1e-8)
    mu=z[80:120]
    pen=1/(1+np.exp(-z[120])); pen=0 if pen<.01 else np.clip(pen,1e-8,1-1e-8)
    return p,sigma,rho,mu,pen

def sample(logits, rng, bias=.5):
    p,sigma,rho,mu,pen=distribution(logits,bias)
    k=rng.choice(20,p=p); z1,z2=rng.standard_normal(2)
    x=mu[k]+sigma[k]*z1
    y=mu[k+20]+sigma[k+20]*(rho[k]*z1+np.sqrt(1-rho[k]**2)*z2)
    return torch.tensor([[x,y,float(rng.random()<pen)]],dtype=torch.float32)

@torch.no_grad()
def generate(models, sentence, seed=42, capture=None, bias=.5):
    chars=encode(sentence); state=zero_state(); xy=torch.tensor([[0.,0.,1.]])
    rng=np.random.default_rng(seed); result=[]
    for t in range(40*len(sentence)):
        state,logits,phi=step(models,state,xy,chars,capture)
        # Original termination_condition uses an independent pen Bernoulli draw.
        pen=distribution(logits,bias)[-1]
        if int(phi.argmax())>=len(chars)-1 and rng.random()<pen:
            break
        # Upstream emits zero when finishing; its demo filters that zero out.
        # Keep only the generated points, stopping before the finishing sample.
        xy=sample(logits,rng,bias); result.append(xy.numpy()[0])
    return np.array(result),state
