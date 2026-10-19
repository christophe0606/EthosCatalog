// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "ImageTypes.h"
#ifdef __cplusplus
extern "C" {
#endif
// Source stride is in uint16_t pixels; output is packed crop_width*crop_height RGB.
// Caller supplies complete source/output storage; returns -1 for invalid geometry.
int rgb565_center_crop(const uint16_t *src,uint32_t width,uint32_t height,uint32_t stride,
                      uint32_t crop_width,uint32_t crop_height,uint8_t *out);
#ifdef __cplusplus
}
#ifndef CMSIS_STREAM_C_ONLY
#include "NodeSupport.hpp"
#include <new>
template<typename IN,int ni,typename OUT,int no>
class Rgb565CenterCrop : public GenericNode<IN,ni,OUT,no> {
public:
    Rgb565CenterCrop(FIFOBase<IN>& in,FIFOBase<OUT>& out,const Rgb565CenterCropParams& p)
        : GenericNode<IN,ni,OUT,no>(in,out),width_(p.width),height_(p.height) {
        static_assert(ni==1 && no==1 && std::is_same_v<IN,Rgb565Image> && std::is_same_v<OUT,Rgb888Image>);
        if(width_>0 && height_>0 && uint64_t(width_)*height_<=UINT32_MAX/3)
            pixels_.reset(new(std::nothrow) uint8_t[size_t(width_)*height_*3]);
    }
    cg_status init() final { return pixels_ ? CG_SUCCESS : CG_MEMORY_ALLOCATION_FAILURE; }
    int run() final {
        const auto frame=*this->getReadBuffer();
        if(!pixels_ || rgb565_center_crop(frame.pixels,frame.width,frame.height,frame.stride,width_,height_,pixels_.get()))
            return CG_INIT_FAILURE;
        *this->getWriteBuffer()={pixels_.get(),uint32_t(width_),uint32_t(height_),uint32_t(width_)*3,frame.sequence,frame.started};
        return CG_SUCCESS;
    }
private:
    int32_t width_,height_;
    std::unique_ptr<uint8_t[]> pixels_;
};
#endif
#endif
