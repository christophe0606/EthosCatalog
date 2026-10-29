#!/usr/bin/env python3
# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Generate a contiguous held-out PCM sequence and independent host PTQ reference."""
from pathlib import Path
import sys,torch,yaml,numpy as np
ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'scripts'))
from quantized_model import load_quantized_program
from quantize import waveform,frames

def main():
 torch.set_num_threads(1)
 project=Path(__file__).resolve().parents[1]
 network=yaml.safe_load((project/'model/network.yml').read_text())['network']
 quantized=load_quantized_program('ethos-u85-256',project,network).module(check_guards=False)
 a=torch.cat((waveform()[480000:480000+128*512],torch.zeros(64*512)))
 h=torch.zeros(1,128);c=h.clone();prob=[]
 with torch.no_grad():
  for x in frames(a):
   p,h,c=quantized(x,h,c);prob.append(p.item())
 pcm=(a.numpy()*32768).astype(np.int16).tolist()
 lines=['// Copyright 2026 Arm Limited and/or its affiliates.','// SPDX-License-Identifier: Apache-2.0','#pragma once','#include <stdint.h>','#define SILERO_STREAM_TEST_COUNT 192U','// Held-out test.wav at30s (128 blocks), then64 zero PCM blocks; zero initial context/state.','static const int16_t silero_stream_pcm[192*512]={']
 for i in range(0,len(pcm),16):lines.append(','.join(map(str,pcm[i:i+16]))+',')
 lines+=['};','static const float silero_stream_expected[192]={']
 for i in range(0,len(prob),8):lines.append(','.join(f'{p:.9e}f' for p in prob[i:i+8])+',')
 lines+=['};']
 (project/'model/stream_test_data.h').write_text('\n'.join(lines)+'\n')
 print('frames',len(prob),'positive',sum(p>=.5 for p in prob),'negative',sum(p<.5 for p in prob),'minmax',min(prob),max(prob))
if __name__=='__main__':main()
