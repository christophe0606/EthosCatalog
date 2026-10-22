// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <initializer_list>
namespace handwriting {
inline uint16_t ink_color(unsigned coverage) {
    return uint16_t(((coverage>>3)<<11)|((coverage>>2)<<5)|(coverage>>3));
}
// A round 1.8-pixel pen with a one-pixel coverage transition. Keep fractional
// endpoints; quantizing them first makes small loops and punctuation angular.
// Iterate a narrow band along the major axis, rather than the bounding box.
template<class Pixel> bool draw_ink(float x0,float y0,float x1,float y1,Pixel pixel) {
    for(float v:{x0,y0,x1,y1}) {
        uint32_t bits;std::memcpy(&bits,&v,sizeof(bits));
        if((bits&0x7f800000U)==0x7f800000U || v < -4096 || v > 4096) return false;
    }
    if(std::fabs(x1-x0)>1600 || std::fabs(y1-y0)>960) return false;
    const bool steep=std::fabs(y1-y0)>std::fabs(x1-x0);
    if(steep) { std::swap(x0,y0);std::swap(x1,y1); }
    if(x1<x0) { std::swap(x0,x1);std::swap(y0,y1); }
    const float dx=x1-x0,dy=y1-y0,length2=dx*dx+dy*dy;
    const float inverse=length2>0?1.0f/length2:0;
    for(int x=int(std::floor(x0-1.4f));x<=int(std::ceil(x1+1.4f));++x) {
        const float t=dx>0?std::clamp((x-x0)/dx,0.0f,1.0f):0;
        const float center=y0+t*dy;
        for(int y=int(std::floor(center-2.0f));y<=int(std::ceil(center+2.0f));++y) {
            const float projection=std::clamp(((x-x0)*dx+(y-y0)*dy)*inverse,0.0f,1.0f);
            const float ex=x-x0-projection*dx,ey=y-y0-projection*dy;
            const float distance2=ex*ex+ey*ey;
            if(distance2>=1.4f*1.4f) continue;
            const float coverage=std::min(1.0f,1.4f-std::sqrt(distance2));
            const auto color=ink_color(unsigned(coverage*255.0f+.5f));
            if(color) pixel(steep?y:x,steep?x:y,color);
        }
    }
    return true;
}
}
