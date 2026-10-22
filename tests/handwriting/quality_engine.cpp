// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
// Host-only bridge: use the production recurrence and sampler with Python dense
// methods. This file is never part of the firmware.
#include "handwriting_engine.hpp"
#include "handwriting_layout.hpp"
#include "handwriting_ink.hpp"
#include <cstring>
#include <algorithm>
#ifdef _WIN32
#define EXPORT extern "C" __declspec(dllexport)
#else
#define EXPORT extern "C"
#endif
using DenseCallback=int (*)(unsigned,const float*,float*);
static DenseCallback callback=nullptr;
static handwriting::Engine engine(nullptr);
namespace handwriting {
int Engine::dense_view(unsigned method,const float *input,const float *&output,uint32_t*) {
    static float storage[1600];
    output=nullptr;
    int error=callback(method,input,storage);
    if(!error) output=storage;
    return error;
}
}
EXPORT int quality_begin(DenseCallback dense,const char *text,unsigned seed,float bias) {
    callback=dense;
    return engine.begin(text,seed,bias);
}
EXPORT int quality_step(float *point,float *attention) {
    handwriting::Stroke stroke{};
    int status=engine.step(stroke);
    point[0]=stroke.dx;point[1]=stroke.dy;point[2]=stroke.pen_up;
    const auto &state=engine.state();
    std::copy(state.phi,state.phi+state.length,attention);
    return status;
}
// Produce a logical 800x480 RGB565 image with the actual LCD geometry/pen.
// mode 0 retains the former three-pixel integer brush for comparison.
EXPORT void quality_render(const float *points,const float *attention,unsigned count,
                          const char *text,unsigned mode,uint16_t *pixels) {
    std::fill(pixels,pixels+800*480,0);
    handwriting::Layout layout;
    const unsigned length=unsigned(std::strlen(text))+1;
    for(unsigned i=0;i<count;++i) {
        unsigned character=0;
        for(unsigned j=1;j<length;++j) if(attention[i*length+j]>attention[i*length+character]) character=j;
        const bool punctuation=handwriting::is_punctuation(text[std::min(character,length-2)]) ||
                               handwriting::punctuation_suffix(text,character);
        const bool word=character<length-1 && text[character]==' ' && !(mode && punctuation);
        auto s=layout.add(points[3*i],points[3*i+1],points[3*i+2],character,word,mode && punctuation);
        if(s.full) break;
        if(!s.ink || s.rejected) continue;
        auto pixel=[&](int x,int y,uint16_t color) {
            if(x>=0 && x<800 && y>=0 && y<480) pixels[y*800+x]=std::max(pixels[y*800+x],color);
        };
        if(mode) handwriting::draw_ink(s.fx0,s.fy0,s.fx1,s.fy1,pixel);
        else handwriting::draw_line(s.x0,s.y0,s.x1,s.y1,[&](int x,int y){pixel(x,y,65535);});
    }
}
