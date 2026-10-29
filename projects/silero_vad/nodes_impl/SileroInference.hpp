// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "../../../common/nodes_impl/c/NodeSupport.hpp"
#include "embedded_silero.h"
// Model-specific context and recurrent state stay with the inference node.
template<typename IN,int inputSamples,typename OUT,int outputSamples>
class SileroInference : public GenericNode<IN,inputSamples,OUT,outputSamples> {
public:
 SileroInference(FIFOBase<IN>& in,FIFOBase<OUT>& out,const SileroInferenceParams& p)
 : GenericNode<IN,inputSamples,OUT,outputSamples>(in,out),p_(p) {
  static_assert(std::is_same_v<IN,float> && std::is_same_v<OUT,float> && inputSamples==512 && outputSamples==1);
 }
 cg_status init() final {return p_.hw_.module && p_.hw_.stats ? CG_SUCCESS:CG_INIT_FAILURE;}
 int run() final {
  const auto* input=this->getReadBuffer(); auto* out=this->getWriteBuffer(); auto& s=*p_.hw_.stats;
  if(!started_ || gap_!=s.audio_discontinuities) {
   std::memset(audio_,0,sizeof(audio_));std::memset(h_,0,sizeof(h_));std::memset(c_,0,sizeof(c_));
   gap_=s.audio_discontinuities; started_=true; ++s.state_resets;
  }
  std::memcpy(audio_+64,input,512*sizeof(float));
  uint32_t start=osKernelGetSysTimerCount();
  int error=silero_forward(p_.hw_.module,audio_,h_,c_,out,h_,c_);
  uint32_t ticks=osKernelGetSysTimerCount()-start;
  if(error)return error;
  uint32_t bits; std::memcpy(&bits,out,sizeof(bits));
  if((bits&0x7f800000U)==0x7f800000U || *out<0 || *out>1)return -420;
  std::memmove(audio_,audio_+512,64*sizeof(float));
  ++s.inferences;s.last_probability=*out;
  if(ticks>s.inference_ticks_max)s.inference_ticks_max=ticks;
  return CG_SUCCESS;
 }
private:
 SileroInferenceParams p_; bool started_=false; uint32_t gap_=0;
 alignas(32) float audio_[576]{},h_[128]{},c_[128]{};
};
