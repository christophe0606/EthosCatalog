// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "ImageTypes.h"
#ifdef __cplusplus
extern "C" {
#endif
// BT.601 float32 luma, left-associated, truncated to uint8 then offset -128.
int rgb888_to_gray_int8(const uint8_t *src,uint32_t width,uint32_t height,uint32_t stride,int8_t *out);
#ifdef __cplusplus
}
#ifndef CMSIS_STREAM_C_ONLY
#include "NodeSupport.hpp"
#include <new>
template<typename IN,int ni,typename OUT,int no>
class Rgb888ToGrayInt8 : public GenericNode<IN,ni,OUT,no> {
public:
    Rgb888ToGrayInt8(FIFOBase<IN>& in,FIFOBase<OUT>& out,const Rgb888ToGrayInt8Params& p)
        : GenericNode<IN,ni,OUT,no>(in,out),capacity_(p.max_pixels) {
        static_assert(ni==1 && no==1 && std::is_same_v<IN,Rgb888Image> && std::is_same_v<OUT,GrayInt8Image>);
        if(capacity_>0) pixels_.reset(new(std::nothrow) int8_t[capacity_]);
    }
    cg_status init() final { return pixels_ ? CG_SUCCESS : CG_MEMORY_ALLOCATION_FAILURE; }
    int run() final {
        const auto frame=*this->getReadBuffer();
        if(!pixels_ || uint64_t(frame.width)*frame.height>uint32_t(capacity_) ||
           rgb888_to_gray_int8(frame.pixels,frame.width,frame.height,frame.stride,pixels_.get()))
            return CG_INIT_FAILURE;
        *this->getWriteBuffer()={pixels_.get(),frame.width,frame.height,frame.width,frame.sequence,frame.started};
        return CG_SUCCESS;
    }
private:
    int32_t capacity_;
    std::unique_ptr<int8_t[]> pixels_;
};
#endif
#endif
