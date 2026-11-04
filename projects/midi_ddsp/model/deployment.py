# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Faithful reusable-cell methods after the documented whole-graph failure.

All methods retain original learned weights/equations. Context passes and
autoregression are scheduled by the caller, preserving whole-block semantics.
"""
import copy
import torch
from torch import nn
from model import load_model

FRAMES=1000


class PackedCell(nn.Module):
    def __init__(self,cell):
        super().__init__();self.cell=cell;self.inputs=cell.kernel.shape[1]

    def forward(self,x):
        return self.cell(x[:,:self.inputs],x[:,self.inputs:])


class ExpressionStep(nn.Module):
    def __init__(self,model):
        super().__init__();self.rnn1=model.rnn1;self.rnn2=model.rnn2;self.norm=model.norm;self.out=model.out

    def forward(self,x):
        h1=self.rnn1(x[:,:262],x[:,262:390]);h2=self.rnn2(h1,x[:,390:518])
        expression=self.out(self.norm(h2[:,None,:]))[:,0]
        # Caller masks this six-value output to zero for pitch-zero rest notes,
        # then uses the masked output for the next autoregressive note.
        return torch.cat([expression,h1,h2],-1)


class PitchStep(nn.Module):
    def __init__(self,model):
        super().__init__();self.rnn1=model.rnn1;self.rnn2=model.rnn2;self.norm=model.norm;self.out=model.pitch_logits

    def forward(self,x):
        h1=self.rnn1(x[:,:713],x[:,713:969]);h2=self.rnn2(h1,x[:,969:1225])
        logits=self.out(self.norm(h2[:,None,:]))[:,0]
        return torch.cat([logits,h1,h2],-1)


SHAPES={'expression_forward':(192+128,128),'expression_backward':(192+128,128),
        'expression_step':(518,262),'precondition':(10,256),
        'context_forward':(320+256,256),'context_backward':(320+256,256),
        'pitch_step':(1225,713)}


def methods(model):
    return {'expression_forward':PackedCell(model.expression.context.forward_cell),
            'expression_backward':PackedCell(model.expression.context.backward_cell),
            'expression_step':ExpressionStep(model.expression),
            'precondition':model.synthesis.pre,
            'context_forward':PackedCell(model.synthesis.context.forward_cell),
            'context_backward':PackedCell(model.synthesis.context.backward_cell),
            'pitch_step':PitchStep(model.synthesis),
            'harmonic':model.synthesis.harmonic}


def load_saved_method(name,path):
    """Load the saved ABI and permit held one-row precondition comparisons."""
    program=torch.export.load(path)
    module=program.module(check_guards=False)
    if name=='precondition':
        # The plain graph keeps identical operators/tensors while removing the
        # exported input hook. Full schedules still use the saved batch size;
        # isolated held-row comparisons can evaluate the same independent map.
        module=torch.fx.GraphModule(module,copy.deepcopy(module.graph))
        module._midi_precondition_batch=int(program.example_inputs[0][0].shape[0])
    return module


def saved_precondition_batch(op):
    if hasattr(op,'_midi_precondition_batch'): return op._midi_precondition_batch
    if hasattr(op,'op'): return saved_precondition_batch(op.op) # Trace wrapper.
    return 1


def run_context(forward,backward,sequence,capture,names):
    size=forward.cell.hidden if isinstance(forward,PackedCell) else 0
    # Quantized GraphModules carry no original cell metadata; output width is
    # provided by the named method ABI.
    size=SHAPES[names[0]][1]
    a=sequence.new_zeros((1,size));b=torch.zeros_like(a);aa=[];bb=[]
    for i in range(sequence.shape[1]):
        x=torch.cat([sequence[:,i],a],-1);y=torch.cat([sequence[:,-1-i],b],-1)
        if capture is not None:
            capture[names[0]].append(x.detach().clone());capture[names[1]].append(y.detach().clone())
        a=forward(x);b=backward(y);aa.append(a);bb.append(b)
    return torch.cat([torch.stack(aa,1),torch.stack(bb[::-1],1)],-1)


def render_controls(model,ops,notes,durations,inst,mapping,frame_features,uniform,capture=None,precondition_batch=None):
    """Full learned schedule; returns raw controls,pitches,expression,logits."""
    def invoke(name,x):
        if capture is not None:capture[name].append(x.detach().clone())
        return ops[name](x)
    e=model.expression;s=model.synthesis
    embeds=torch.cat([e.pitch(notes),e.duration(durations),e.instrument(inst)[:,None].expand(-1,notes.shape[1],-1)],-1)
    context=run_context(ops['expression_forward'],ops['expression_backward'],embeds,capture,('expression_forward','expression_backward'))
    h1=context.new_zeros((1,128));h2=torch.zeros_like(h1);previous=context.new_zeros((1,6));expressions=[]
    for i in range(notes.shape[1]):
        out=invoke('expression_step',torch.cat([context[:,i],previous,h1,h2],-1))
        previous=out[:,:6]*(notes[:,i:i+1]!=0);h1=out[:,6:134];h2=out[:,134:262];expressions.append(previous)
    expression=torch.stack(expressions,1)
    features=torch.cat([expression.clamp(0.,1.)[:,mapping[0]],frame_features],-1)
    precondition=[]
    batch=saved_precondition_batch(ops['precondition']) if precondition_batch is None else precondition_batch
    if batch<1: raise ValueError('Precondition batch must be positive')
    for i in range(0,features.shape[1],batch):
        valid=min(batch,features.shape[1]-i)
        rows=features[0,i:i+valid]
        if valid<batch: rows=torch.cat([rows,rows.new_zeros(batch-valid,rows.shape[-1])])
        precondition.append(invoke('precondition',rows)[:valid])
    z=torch.cat([torch.cat(precondition,0)[None],s.instrument(inst)[:,None].expand(-1,features.shape[1],-1)],-1)
    context=run_context(ops['context_forward'],ops['context_backward'],z,capture,('context_forward','context_backward'))
    h1=z.new_zeros((1,256));h2=torch.zeros_like(h1);previous=z.new_zeros((1,201));pitches=[];logits_all=[]
    for i in range(z.shape[1]):
        out=invoke('pitch_step',torch.cat([context[:,i],previous,h1,h2],-1))
        logits=out[:,:201];h1=out[:,201:457];h2=out[:,457:713]
        index=s.sample(logits,uniform[:,i]);previous=torch.nn.functional.one_hot(index,201).to(z.dtype)
        pitches.append(frame_features[:,i,0:1]*127.+index[:,None].to(z.dtype)*.01-1.)
        logits_all.append(logits)
    pitches=torch.stack(pitches,1)
    harmonic_input=torch.cat([z,s.pitch_embedding(pitches/127.)],-1)
    raw=invoke('harmonic',harmonic_input)
    return raw,pitches,expression,torch.stack(logits_all,1)


def score(inst,frames,seed):
    generator=torch.Generator().manual_seed(seed)
    # The supported instruments have differing natural ranges. Include rests,
    # repeated notes and transitions without claiming a real held-out dataset.
    notes=torch.tensor([[48+inst,55+inst,0,60+inst]],dtype=torch.int64)
    mapping=(torch.arange(frames)*4//frames)[None]
    durations=torch.full((1,4,1),frames/4/250.)
    features=torch.zeros((1,frames,4));features[0,:,0]=notes[0,mapping[0]].float()/127.
    for note in range(4):
        selected=torch.where(mapping[0]==note)[0];features[0,selected[0],1]=1;features[0,selected[-1],2]=1
        # Original note masks exclude pitch-zero regions by default.
        if int(notes[0,note]) != 0:
            features[0,selected,3]=torch.arange(1,len(selected)+1)/len(selected)
    return notes,durations,torch.tensor([inst]),mapping,features,torch.rand((1,frames,1),generator=generator)*.98+.01
