// Copyright (c) 2022 Arm Limited. All rights reserved.
// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
// Arithmetic translated from MLEK ImageUtils.cc; see object_detection/model/NOTICE.md.
#include "Rgb888ToGrayInt8.hpp"
int rgb888_to_gray_int8(const uint8_t *src,uint32_t width,uint32_t height,uint32_t stride,int8_t *out)
{
    if(!src || !out || !width || !height || width>UINT32_MAX/3 || stride<width*3 ||
       (uint64_t)stride*height>SIZE_MAX || (uint64_t)width*height>SIZE_MAX) return -1;
    for(uint32_t y=0;y<height;++y) for(uint32_t x=0;x<width;++x) {
        const uint8_t *p=src+(size_t)y*stride+3*x;
        // Explicit intermediates preserve source float32 truncation under fast math.
        volatile float r=0.299f*p[0],g=0.587f*p[1],b=0.114f*p[2];
        volatile float rg=r+g;
        float gray=rg+b;
        out[(size_t)y*width+x]=(int8_t)((int)(uint8_t)gray-128);
    }
    return 0;
}
