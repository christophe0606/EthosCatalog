// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
// Private backend banks for this project's single admitted instrument stream.
// The startup profiler may borrow them only before this node starts.
typedef struct {float *coefficients;float *history;} midi_reverb_banks_t;
midi_reverb_banks_t midi_reverb_banks(void);
#ifdef __cplusplus
}
#endif
#if defined(__cplusplus) && !defined(MIDI_REVERB_BACKEND_ONLY)
#include "../../../common/nodes_impl/c/NodeSupport.hpp"
#include "app_params.h"
#include "c/ddsp_reverb256.hpp"
template<typename IN,int n,typename OUT,int no>class DDSPReverb:public GenericNode<IN,n,OUT,no>{
 HardwareParams hw_;midi_ddsp::Reverb256 reverb_;const float*last_=nullptr;
public:DDSPReverb(FIFOBase<IN>&i,FIFOBase<OUT>&o,const DDSPReverbParams&p):GenericNode<IN,n,OUT,no>(i,o),hw_(p.hw_){}
 int run()final{const float*ir=hw_.impulse(hw_.network);if(ir!=last_){auto banks=midi_reverb_banks();reverb_.init(banks.coefficients,banks.history,ir,true);last_=ir;}
 reverb_.process256(this->getReadBuffer(),this->getWriteBuffer());return CG_SUCCESS;}
};
#endif
