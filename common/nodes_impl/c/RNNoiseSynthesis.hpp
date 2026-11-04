// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "NodeSupport.hpp"
#include "rnnoise/rnnoise_dsp.h"
template<typename A,int na,typename B,int nb,typename OUT,int no>
class RNNoiseSynthesis:public GenericNode21<A,na,B,nb,OUT,no>{
 void* state_=nullptr;RNNoiseSynthesisParams p_;uint32_t epoch_=0;
public:
 RNNoiseSynthesis(FIFOBase<A>& a,FIFOBase<B>& b,FIFOBase<OUT>& o,const RNNoiseSynthesisParams& p):GenericNode21<A,na,B,nb,OUT,no>(a,b,o),p_(p){static_assert(na==RN_CONTEXT&&nb==23&&no==RN_RESULT);}
 ~RNNoiseSynthesis(){rn_dsp_destroy(state_);}
 cg_status init() final {state_=rn_dsp_create();return state_?CG_SUCCESS:CG_MEMORY_ALLOCATION_FAILURE;}
 int run() final {auto* a=this->getReadBuffer1();auto* b=this->getReadBuffer2();auto* o=this->getWriteBuffer();uint32_t t=osKernelGetSysTimerCount(),epoch=uint32_t(a[1991]);if(epoch!=epoch_){rn_dsp_reset(state_);epoch_=epoch;}rn_synthesize(state_,a,b,o);if(p_.hw_.synthesis_observed)p_.hw_.synthesis_observed(osKernelGetSysTimerCount()-t);return CG_SUCCESS;}
};
