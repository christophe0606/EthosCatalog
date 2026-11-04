// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "../../../common/nodes_impl/c/NodeSupport.hpp"
#include "embedded_rnnoise.h"
#include <cmath>
template<typename IN,int ni,typename OUT,int no>
class RNNoiseInference:public GenericNode<IN,ni,OUT,no>{
 RNNoiseInferenceParams p_;RNNoiseState state_{};int8_t features_[42],output_[191];uint32_t epoch_=0;
public:
 RNNoiseInference(FIFOBase<IN>& i,FIFOBase<OUT>& o,const RNNoiseInferenceParams& p):GenericNode<IN,ni,OUT,no>(i,o),p_(p){static_assert(ni==42&&no==23);}
 cg_status init() final {rnnoise_reset(state_);return p_.hw_.module&&p_.hw_.stats?CG_SUCCESS:CG_INIT_FAILURE;}
 int run() final {auto* x=this->getReadBuffer();auto* y=this->getWriteBuffer();auto& s=*p_.hw_.stats;uint32_t t=osKernelGetSysTimerCount();uint32_t epoch=p_.hw_.audio_epoch();if(epoch!=epoch_){rnnoise_reset(state_);epoch_=epoch;++s.state_resets;}
 for(unsigned i=0;i<42;++i){float q=std::nearbyint(x[i]/0.22150060534477234f)+14;features_[i]=int8_t(std::max(-128.f,std::min(127.f,q)));}
 int e=rnnoise_forward(p_.hw_.module,features_,state_,output_);if(e)return e;for(unsigned i=0;i<22;++i)y[i]=(int(output_[96+i])+128)/256.f;y[22]=(int(output_[190])+128)/256.f;
 ++s.inferences;s.last_probability=y[22];uint32_t dt=osKernelGetSysTimerCount()-t;if(dt>s.inference_ticks_max)s.inference_ticks_max=dt;return CG_SUCCESS;}
};
