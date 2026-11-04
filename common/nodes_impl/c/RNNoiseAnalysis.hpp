// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "NodeSupport.hpp"
#include "rnnoise/rnnoise_dsp.h"
template<typename IN,int ni,typename A,int na,typename B,int nb>
class RNNoiseAnalysis:public GenericNode12<IN,ni,A,na,B,nb>{
 void* state_=nullptr;RNNoiseAnalysisParams p_;uint32_t epoch_=0;
public:
 RNNoiseAnalysis(FIFOBase<IN>& i,FIFOBase<A>& a,FIFOBase<B>& b,const RNNoiseAnalysisParams& p):GenericNode12<IN,ni,A,na,B,nb>(i,a,b),p_(p){static_assert(ni==480&&na==42&&nb==RN_CONTEXT);}
 ~RNNoiseAnalysis(){rn_dsp_destroy(state_);}
 cg_status init() final {state_=rn_dsp_create();return state_&&p_.hw_.audio_epoch?CG_SUCCESS:CG_INIT_FAILURE;}
 int run() final {auto* in=this->getReadBuffer();auto* a=this->getWriteBuffer1();auto* b=this->getWriteBuffer2();uint32_t t=osKernelGetSysTimerCount(),epoch=p_.hw_.audio_epoch();if(epoch!=epoch_){rn_dsp_reset(state_);epoch_=epoch;}rn_analyze(state_,in,a,b,epoch&65535U);if(p_.hw_.analysis_observed)p_.hw_.analysis_observed(osKernelGetSysTimerCount()-t);return CG_SUCCESS;}
};
