// Copyright (c) 2022 Arm Limited. All rights reserved.
// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "stream_types.h"
#define FACE_MAX_CANDIDATES 540
#ifdef __cplusplus
extern "C" {
#endif
uint32_t face_decode(const int8_t *head0,const int8_t *head1,FaceBox *workspace,FaceBox *out);
uint32_t face_decode_candidates(const int8_t *head0,const int8_t *head1,FaceBox *out);
#ifdef __cplusplus
}
#ifndef DETECTION_HOST_TEST
#include "../../../common/nodes_impl/c/NodeSupport.hpp"
template<typename IN,int ni,typename OUT,int no,typename FRAME,int nf>
class FaceDecode : public GenericNode12<IN,ni,OUT,no,FRAME,nf> {
public:
    FaceDecode(FIFOBase<IN>& in,FIFOBase<OUT>& out,FIFOBase<FRAME>& frame,const FaceDecodeParams& p)
        : GenericNode12<IN,ni,OUT,no,FRAME,nf>(in,out,frame),hw_(p.hw_) { static_assert(ni==1 && no==1 && nf==1); }
    int run() final {
        const auto in=*this->getReadBuffer();
        uint32_t n=face_decode_candidates(in.head0,in.head1,boxes_);
        *this->getWriteBuffer1()={boxes_,n,in.frame.sequence,in.frame.started};
        *this->getWriteBuffer2()=in.frame;
        ++hw_.stats->decoded;
        return CG_SUCCESS;
    }
private:
    HardwareParams hw_;
    FaceBox boxes_[FACE_MAX_CANDIDATES];
};
#endif
#endif
