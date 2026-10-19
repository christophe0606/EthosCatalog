// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "../../../common/nodes_impl/c/NodeSupport.hpp"
extern "C" int detection_forward(void *runtime,const int8_t *input,int8_t *head0,int8_t *head1,uint32_t *ticks);
template<typename IN,int ni,typename RGB,int nr,typename OUT,int no>
class FaceInference : public GenericNode21<IN,ni,RGB,nr,OUT,no> {
public:
    FaceInference(FIFOBase<IN>& in,FIFOBase<RGB>& rgb,FIFOBase<OUT>& out,const FaceInferenceParams& p)
        : GenericNode21<IN,ni,RGB,nr,OUT,no>(in,rgb,out),hw_(p.hw_) { static_assert(ni==1 && nr==1 && no==1); }
    cg_status init() final { return hw_.module && hw_.inference_input ? CG_SUCCESS : CG_INIT_FAILURE; }
    int run() final {
        const auto gray=*this->getReadBuffer1(); const auto rgb=*this->getReadBuffer2(); uint32_t ticks;
        if(gray.width!=192 || gray.height!=192 || gray.stride!=192 || rgb.width!=192 ||
           rgb.height!=192 || rgb.stride!=192*3 || gray.sequence!=rgb.sequence || gray.started!=rgb.started)
            return -834;
        ++hw_.stats->cropped; ++hw_.stats->grayscaled; ++hw_.stats->preprocessed;
        int error=detection_forward(hw_.module,gray.pixels,head0_,head1_,&ticks);
        if(error) return error;
        ++hw_.stats->inferences;
        if(ticks>hw_.stats->forward_ticks_max) hw_.stats->forward_ticks_max=ticks;
        *this->getWriteBuffer()={{rgb.pixels,gray.pixels,gray.sequence,gray.started},head0_,head1_};
        return CG_SUCCESS;
    }
private:
    HardwareParams hw_;
    int8_t head0_[648],head1_[2592];
};
