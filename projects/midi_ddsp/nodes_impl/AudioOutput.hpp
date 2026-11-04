// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "../../../common/nodes_impl/c/NodeSupport.hpp"
#include "app_params.h"
#include <cmath>
extern "C" {extern volatile uint32_t midi_audio_nonfinite,midi_audio_clipped,midi_audio_blocks;}
template<typename IN,int n>class AudioOutput:public GenericSink<IN,n>{
 HardwareParams hw_;
public:AudioOutput(FIFOBase<IN>&i,const AudioOutputParams&p):GenericSink<IN,n>(i),hw_(p.hw_){}
 int run()final{const float*in=this->getReadBuffer();int16_t pcm[n];for(int j=0;j<n;++j){float v=in[j];uint32_t bits;std::memcpy(&bits,&v,4);if((bits&0x7f800000U)==0x7f800000U){++midi_audio_nonfinite;return CG_BUFFER_ERROR;}if(v>1||v< -1)++midi_audio_clipped;v=std::max(-1.f,std::min(1.f,v));pcm[j]=int16_t(std::lrint(v*32767));}int e=hw_.audio_write(pcm,n);if(e)return e;++midi_audio_blocks;
  if(hw_.finished(hw_.network)){e=hw_.audio_finish();return e?e:CG_STOP_SCHEDULER;}return CG_SUCCESS;}
};
