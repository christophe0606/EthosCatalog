# Copyright 2026 Arm Limited and/or its affiliates.
# Derived from Silero VAD, Copyright (c) 2020-present Silero Team.
# SPDX-License-Identifier: MIT
"""Fixed-shape 16 kHz Silero VAD; architecture from upstream tinygrad_model.py.

Input audio [1,576] includes 64 previous samples and 512 new samples.
State inputs h,c [1,128]; outputs probability [1,1], h,c [1,128].
"""
from pathlib import Path
import torch
from torch import nn
from torch.nn import functional as F
from safetensors.torch import load_file

class SileroVAD(nn.Module):
    def __init__(self):
        super().__init__()
        self.stft_conv=nn.Conv1d(1,258,256,stride=128,bias=False)
        self.conv1=nn.Conv1d(129,128,3,padding=1)
        self.conv2=nn.Conv1d(128,64,3,stride=2,padding=1)
        self.conv3=nn.Conv1d(64,64,3,stride=2,padding=1)
        self.conv4=nn.Conv1d(64,128,3,padding=1)
        self.lstm_cell=nn.LSTMCell(128,128)
        self.final_conv=nn.Conv1d(128,1,1)
        source=torch.jit.load(str(Path(__file__).parent/'upstream/silero_vad.jit'))._model.state_dict()
        mapping={'stft_conv.weight':'stft.forward_basis_buffer'}
        for n in range(4):
            for kind in ('weight','bias'): mapping[f'conv{n+1}.{kind}']=f'encoder.{n}.reparam_conv.{kind}'
        for kind in ('weight_ih','weight_hh','bias_ih','bias_hh'): mapping['lstm_cell.'+kind]='decoder.rnn.'+kind
        for kind in ('weight','bias'): mapping['final_conv.'+kind]='decoder.decoder.2.'+kind
        self.load_state_dict({name:source[key].clone() for name,key in mapping.items()})

    def forward(self,audio,h,c):
        x=F.pad(audio,(0,64),mode='reflect').unsqueeze(1)
        x=self.stft_conv(x)
        x=torch.sqrt(x[:,:129,:].square()+x[:,129:,:].square())
        x=self.conv1(x).relu()
        x=self.conv2(x).relu()
        x=self.conv3(x).relu()
        x=self.conv4(x).relu().squeeze(-1)
        # Algebraic LSTMCell expansion permits automatic Arm partitioning.
        gates=F.linear(x,self.lstm_cell.weight_ih,self.lstm_cell.bias_ih)+F.linear(h,self.lstm_cell.weight_hh,self.lstm_cell.bias_hh)
        i,f,g,o=gates.chunk(4,dim=1)
        c=f.sigmoid()*c+i.sigmoid()*g.tanh()
        h=o.sigmoid()*c.tanh()
        probability=self.final_conv(h.relu().unsqueeze(-1)).sigmoid().squeeze(1).mean(1,keepdim=True)
        return probability,h,c
