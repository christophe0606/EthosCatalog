// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "Rgb565CenterCrop.hpp"
int rgb565_center_crop(const uint16_t *src,uint32_t width,uint32_t height,uint32_t stride,
                      uint32_t crop_width,uint32_t crop_height,uint8_t *out)
{
    if(!src || !out || !crop_width || !crop_height || crop_width>width || crop_height>height || stride<width ||
       (uint64_t)stride*height>SIZE_MAX/sizeof(uint16_t) || (uint64_t)crop_width*crop_height>SIZE_MAX/3)
        return -1;
    uint32_t left=(width-crop_width)/2,top=(height-crop_height)/2;
    for(uint32_t y=0;y<crop_height;++y) for(uint32_t x=0;x<crop_width;++x) {
        uint16_t p=src[(size_t)(top+y)*stride+left+x];
        uint32_t r=(p>>11)&31,g=(p>>5)&63,b=p&31;
        size_t i=3*((size_t)y*crop_width+x);
        out[i]=(uint8_t)((r<<3)|(r>>2));
        out[i+1]=(uint8_t)((g<<2)|(g>>4));
        out[i+2]=(uint8_t)((b<<3)|(b>>2));
    }
    return 0;
}
