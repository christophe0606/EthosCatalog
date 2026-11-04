# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Inference-only PyTorch reconstruction of pretrained MIDI-DDSP.

Architecture and checkpoint naming follow the Apache-2.0 MIDI-DDSP/DDSP
reference files in reference/. No training or replacement model is involved.
Inputs are complete score segments; the original synthesis context and custom
normalization are noncausal. Exposing a streaming scheduler is a separate step.
"""
from pathlib import Path
import torch
from torch import nn
from torch.nn import functional as F

HERE = Path(__file__).resolve().parent
SUFFIX = '/.ATTRIBUTES/VARIABLE_VALUE'
PREFIX = 'midi_decoder/decoder/'
INSTRUMENTS = ('vn','va','vc','db','fl','ob','cl','sax','bn','tpt','hn','tbn','tba')


class Weights:
    def __init__(self, tensors):
        self.tensors = tensors

    def get(self, key):
        return self.tensors[key]


class Dense(nn.Module):
    def __init__(self, w, key):
        super().__init__()
        self.weight = nn.Parameter(w.get(key+'/kernel').T.contiguous(), False)
        self.bias = nn.Parameter(w.get(key+'/bias').clone(), False)

    def forward(self, x):
        return F.linear(x, self.weight, self.bias)


class Norm(nn.Module):
    def __init__(self, w, key, keras=False):
        super().__init__()
        self.scale = nn.Parameter(w.get(key+('/gamma' if keras else '/scale')).flatten(), False)
        self.shift = nn.Parameter(w.get(key+('/beta' if keras else '/shift')).flatten(), False)
        self.keras = keras

    def forward(self, x):
        # Keras LayerNormalization defaults eps=.001 and last axis. DDSP's
        # Normalize('layer') has eps=1e-5 and includes the entire time axis.
        axes = (-1,) if self.keras else tuple(range(1, x.ndim))
        mean = x.mean(axes, keepdim=True)
        variance = ((x-mean)**2).mean(axes, keepdim=True)
        return (x-mean)*torch.rsqrt(variance+(0.001 if self.keras else 1e-5))*self.scale+self.shift


class FcStack(nn.Module):
    def __init__(self, w, key, count):
        super().__init__()
        self.dense = nn.ModuleList()
        self.norm = nn.ModuleList()
        for i in range(count):
            base = key+f'/stack/layer_with_weights-{i}'
            self.dense.append(Dense(w, base+'/layer_with_weights-0'))
            self.norm.append(Norm(w, base+'/layer_with_weights-1', keras=True))
        self.out = Dense(w, key+'/dense_out')

    def forward(self, x):
        for d,n in zip(self.dense,self.norm):
            x = F.leaky_relu(n(d(x)), negative_slope=0.2)
        return self.out(x)


class GRUCell(nn.Module):
    """Keras reset_after=True gate order z,r,h (different from torch GRU)."""
    def __init__(self, w, key):
        super().__init__()
        self.kernel = nn.Parameter(w.get(key+'/cell/kernel').T.contiguous(), False)
        self.recurrent = nn.Parameter(w.get(key+'/cell/recurrent_kernel').T.contiguous(), False)
        self.bias = nn.Parameter(w.get(key+'/cell/bias').clone(), False)
        self.hidden = self.recurrent.shape[1]

    def forward(self, x, h):
        xz,xr,xh = F.linear(x, self.kernel, self.bias[0]).chunk(3,-1)
        hz,hr,hh = F.linear(h, self.recurrent, self.bias[1]).chunk(3,-1)
        z = torch.sigmoid(xz+hz)
        r = torch.sigmoid(xr+hr)
        candidate = torch.tanh(xh+r*hh)
        return z*h+(1-z)*candidate


class BiGRU(nn.Module):
    def __init__(self, w, key):
        super().__init__()
        self.forward_cell = GRUCell(w,key+'/forward_layer')
        self.backward_cell = GRUCell(w,key+'/backward_layer')

    def forward(self,x):
        a = x.new_zeros((x.shape[0],self.forward_cell.hidden))
        b = torch.zeros_like(a)
        forward,backward = [],[]
        for i in range(x.shape[1]):
            a = self.forward_cell(x[:,i],a)
            b = self.backward_cell(x[:,-1-i],b)
            forward.append(a)
            backward.append(b)
        return torch.cat([torch.stack(forward,1),torch.stack(backward[::-1],1)],-1)


class Expression(nn.Module):
    def __init__(self,w):
        super().__init__()
        self.pitch = nn.Embedding.from_pretrained(w.get('pitch_emb/embeddings'),freeze=True)
        self.instrument = nn.Embedding.from_pretrained(w.get('instrument_emb/embeddings'),freeze=True)
        self.duration = Dense(w,'duration_emb')
        self.context = BiGRU(w,'birnn')
        self.rnn1 = GRUCell(w,'rnn1')
        self.rnn2 = GRUCell(w,'rnn2')
        self.norm = Norm(w,'norm')
        self.out = FcStack(w,'dense_out/dense_out',2)

    def forward(self,pitch,duration,instrument):
        z = torch.cat([self.pitch(pitch),self.duration(duration),
                       self.instrument(instrument)[:,None,:].expand(-1,pitch.shape[1],-1)],-1)
        z = self.context(z)
        h1 = z.new_zeros((z.shape[0],128)); h2 = torch.zeros_like(h1)
        previous = z.new_zeros((z.shape[0],6)); outputs=[]
        for i in range(z.shape[1]):
            h1 = self.rnn1(torch.cat([z[:,i],previous],-1),h1)
            h2 = self.rnn2(h1,h2)
            previous = self.out(self.norm(h2[:,None,:]))[:,0]*(pitch[:,i:i+1]!=0)
            outputs.append(previous)
        return torch.stack(outputs,1)


class Harmonic(nn.Module):
    def __init__(self,w):
        super().__init__()
        key=PREFIX+'midi_f0_to_harmonic'
        self.input = self.conv(w,key+'/net/conv_in',1)
        self.layers=nn.ModuleList([self.conv(w,key+f'/net/layers/{i}/layer_with_weights-0',2**(i%5)) for i in range(20)])
        self.norms=nn.ModuleList([Norm(w,key+f'/net/norms/{i}') for i in range(20)])
        self.norm=Norm(w,key+'/norm');self.out=Dense(w,key+'/dense_out')

    @staticmethod
    def conv(w,key,dilation):
        kernel=w.get(key+'/kernel').permute(3,2,0,1).contiguous()
        layer=nn.Conv2d(kernel.shape[1],kernel.shape[0],(3,1),padding=(dilation,0),dilation=(dilation,1))
        layer.weight=nn.Parameter(kernel,False);layer.bias=nn.Parameter(w.get(key+'/bias').clone(),False)
        return layer

    def forward(self,x):
        x=self.input(x.transpose(1,2).unsqueeze(-1)).squeeze(-1).transpose(1,2)
        for layer,norm in zip(self.layers,self.norms):
            y=layer(F.relu(x).transpose(1,2).unsqueeze(-1)).squeeze(-1).transpose(1,2)
            x=x+norm(y)
        return self.out(self.norm(x))


class TopPSample(nn.Module):
    """Nonlearned categorical sampler; keep float to avoid sentinel PTQ ranges."""
    def forward(self,logits,uniform):
        sorted_logits=torch.sort(logits,dim=-1,descending=True).values
        probs=sorted_logits.softmax(-1)
        exclusive=probs.cumsum(-1)-probs
        threshold=torch.where(exclusive<.95,sorted_logits,torch.full_like(logits,1000.)).amin(-1,keepdim=True)
        probs=torch.where(logits<threshold,torch.full_like(logits,-1e10),logits).softmax(-1)
        return (probs.cumsum(-1)<uniform).sum(-1).clamp(max=200)


class Synthesis(nn.Module):
    def __init__(self,w):
        super().__init__()
        self.pre=FcStack(w,'midi_decoder/z_preconditioning_stack',5)
        self.instrument=nn.Embedding.from_pretrained(w.get('midi_decoder/instrument_emb/embeddings'),freeze=True)
        key=PREFIX+'midi_to_f0'
        self.context=BiGRU(w,key+'/birnn'); self.rnn1=GRUCell(w,key+'/rnn1');self.rnn2=GRUCell(w,key+'/rnn2')
        self.norm=Norm(w,key+'/norm');self.pitch_logits=Dense(w,key+'/dense_out')
        self.pitch_embedding=Dense(w,PREFIX+'q_pitch_emb');self.harmonic=Harmonic(w)
        self.sample=TopPSample()

    def forward(self,features,instrument,uniform):
        z=torch.cat([self.pre(features),self.instrument(instrument)[:,None,:].expand(-1,features.shape[1],-1)],-1)
        context=self.context(z)
        h1=z.new_zeros((z.shape[0],256));h2=torch.zeros_like(h1);previous=z.new_zeros((z.shape[0],201))
        pitches=[];all_logits=[]
        for i in range(z.shape[1]):
            h1=self.rnn1(torch.cat([context[:,i],previous],-1),h1);h2=self.rnn2(h1,h2)
            logits=self.pitch_logits(self.norm(h2[:,None,:]))[:,0]
            index=self.sample(logits,uniform[:,i])
            previous=F.one_hot(index,201).to(z.dtype)
            pitch=features[:,i,6:7]*127.+index[:,None].to(z.dtype)*.01-1.
            pitches.append(pitch);all_logits.append(logits)
        pitches=torch.stack(pitches,1)
        raw=self.harmonic(torch.cat([z,self.pitch_embedding(pitches/127.)],-1))
        return raw,pitches,torch.stack(all_logits,1)


class CompleteModel(nn.Module):
    def __init__(self,expression,synthesis):
        super().__init__();self.expression=Expression(expression);self.synthesis=Synthesis(synthesis)

    def forward(self,note_pitch,note_duration,instrument,frame_note,frame_features,uniform):
        expression=self.expression(note_pitch,note_duration,instrument)
        # Upstream inference clips the generated dataframe conditioning only;
        # the expression generator's recurrent feedback above remains raw.
        expanded=expression.clamp(0.,1.)[:,frame_note[0],:]
        features=torch.cat([expanded,frame_features],-1)
        raw,pitch,logits=self.synthesis(features,instrument,uniform)
        return raw,pitch,expression,logits


def load_model(path=HERE/'pretrained.safetensors'):
    from safetensors.torch import load_file
    tensors=load_file(str(path))
    e=Weights({k[2:]:v for k,v in tensors.items() if k.startswith('e/')})
    s=Weights({k[2:]:v for k,v in tensors.items() if k.startswith('s/')})
    return CompleteModel(e,s).eval()
