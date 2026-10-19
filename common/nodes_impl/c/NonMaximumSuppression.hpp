// Copyright (c) 2022 Arm Limited. All rights reserved.
// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "DetectionTypes.h"
#ifdef __cplusplus
extern "C" {
#endif
// Stable descending score order, suppress IoU > threshold before any clipping.
// Scores must be positive; finite boxes with nonnegative widths/heights are
// supported, including zero-area boxes (IoU zero). Caller supplies disjoint
// work/output arrays of capacity boxes. Input may alias work; never output.
int scored_box_nms(const ScoredBox *input,uint32_t count,float threshold,
                   ScoredBox *work,ScoredBox *output,uint32_t capacity,uint32_t *output_count);
#ifdef __cplusplus
}
#ifndef CMSIS_STREAM_C_ONLY
#include "NodeSupport.hpp"
#include <new>
template<typename IN,int ni,typename OUT,int no>
class NonMaximumSuppression : public GenericNode<IN,ni,OUT,no> {
public:
    NonMaximumSuppression(FIFOBase<IN>& in,FIFOBase<OUT>& out,const NonMaximumSuppressionParams& p)
        : GenericNode<IN,ni,OUT,no>(in,out),capacity_(p.max_boxes),threshold_(p.iou_threshold) {
        static_assert(ni==1 && no==1 && std::is_same_v<IN,ScoredBoxList> && std::is_same_v<OUT,ScoredBoxList>);
        if(capacity_>0) {
            work_.reset(new(std::nothrow) ScoredBox[capacity_]);
            output_.reset(new(std::nothrow) ScoredBox[capacity_]);
        }
    }
    cg_status init() final {
        return work_ && output_ && threshold_>=0 && threshold_<=1 ? CG_SUCCESS : CG_INIT_FAILURE;
    }
    int run() final {
        const auto frame=*this->getReadBuffer(); uint32_t count=0;
        if(!work_ || !output_ || scored_box_nms(frame.boxes,frame.count,threshold_,work_.get(),output_.get(),capacity_,&count))
            return CG_INIT_FAILURE;
        *this->getWriteBuffer()={output_.get(),count,frame.sequence,frame.started};
        return CG_SUCCESS;
    }
private:
    int32_t capacity_; float threshold_;
    std::unique_ptr<ScoredBox[]> work_,output_;
};
#endif
#endif
