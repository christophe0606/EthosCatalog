// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <cstring>
namespace handwriting {
inline bool is_punctuation(char value) {
    return value && std::strchr(".,:;!?'\")",value);
}
inline bool punctuation_suffix(const char *text,unsigned character) {
    const unsigned length=unsigned(std::strlen(text));
    if(!length) return false;
    const char *tail=text+std::min(character,length-1);
    bool mark=false;
    for(;*tail;++tail) {
        if(*tail==' ') continue;
        if(!is_punctuation(*tail)) return false;
        mark=true;
    }
    return mark;
}
// Online geometry at fixed readable scale. Prefer spaces, then attended
// character changes at pen lifts; a hard edge also bounds unusual long strokes.
class Layout {
public:
    static constexpr float Scale=3.0f;
    static constexpr int Width=800,Height=480,Margin=30,LineHeight=140,MaxLines=3;
    void reset() { *this=Layout{}; }
    // Generous bounds relative to an 800x480 viewport. Test IEEE bits before
    // floating arithmetic: isfinite alone can be optimized away by fast-math.
    static constexpr float MaxModelDelta=256.0f,MaxState=8192.0f,MaxScreen=4096.0f;
    struct Segment {
        int x0,y0,x1,y1; bool ink,wrapped,full,rejected;
        // Retain subpixel endpoints for the antialiased renderer. Integer
        // endpoints remain available for geometry checks and legacy previews.
        float fx0=0,fy0=0,fx1=0,fy1=0;
    };
    Segment add(float dx,float dy,float pen,unsigned character,bool word,bool punctuation=false) {
        if(!bounded(dx,MaxModelDelta) || !bounded(dy,MaxModelDelta)) return reject();
        // The operands are now bounded, so multiplication and addition cannot
        // overflow. Reject without changing the retained geometry.
        float nx=x_+dx*Scale,ny=y_-dy*Scale;
        if(!bounded(nx,MaxState) || !bounded(ny,MaxState)) return reject();
        float screen_x=nx-origin_x_+Margin;
        bool boundary=previous_up_ && (character!=character_ || word);
        bool wrap=(screen_x>640 && word && previous_up_) ||
                  (screen_x>710 && boundary && !punctuation) || screen_x>Width-Margin;
        unsigned next_line=line_;
        float next_origin_x=origin_x_,next_origin_y=origin_y_;
        if(wrap) {
            if(line_==MaxLines-1) return {0,0,0,0,false,false,true,false};
            ++next_line;next_origin_x=x_;next_origin_y=y_;
        }
        float x0=x_-next_origin_x+Margin,y0=y_-next_origin_y+90+next_line*LineHeight;
        float x1=nx-next_origin_x+Margin,y1=ny-next_origin_y+90+next_line*LineHeight;
        if(!bounded(x0,MaxScreen) || !bounded(y0,MaxScreen) ||
           !bounded(x1,MaxScreen) || !bounded(y1,MaxScreen)) return reject();
        Segment segment{int(std::lround(x0)),int(std::lround(y0)),
            int(std::lround(x1)),int(std::lround(y1)),!previous_up_ && !wrap,wrap,false,false,x0,y0,x1,y1};
        x_=nx;y_=ny;origin_x_=next_origin_x;origin_y_=next_origin_y;line_=next_line;
        previous_up_=pen>=.5f;character_=character;
        return segment;
    }
    unsigned lines() const { return line_+1; }
    static unsigned native_index(unsigned x,unsigned y) { return x*480U+(479U-y); }
private:
    static bool bounded(float value,float limit) {
        static_assert(sizeof(float)==sizeof(uint32_t),"IEEE binary32 coordinates required");
        uint32_t bits;std::memcpy(&bits,&value,sizeof(bits));
        return (bits&0x7f800000U)!=0x7f800000U && value>=-limit && value<=limit;
    }
    Segment reject() {
        // Do not connect the next valid point across an omitted model segment.
        previous_up_=true;return {0,0,0,0,false,false,false,true};
    }
    float x_=0,y_=0,origin_x_=0,origin_y_=0;
    unsigned line_=0,character_=0; bool previous_up_=true;
};
template<class Pixel> inline bool draw_line(int x0,int y0,int x1,int y1,Pixel pixel) {
    // Widen BEFORE subtraction/absolute value, including INT_MIN/MAX callers.
    int64_t wide_dx=int64_t(x1)-int64_t(x0),wide_dy=int64_t(y1)-int64_t(y0);
    if(wide_dx<0) wide_dx=-wide_dx;
    if(wide_dy<0) wide_dy=-wide_dy;
    constexpr int bound=int(Layout::MaxScreen);
    if(wide_dx>1600 || wide_dy>960 || x0 < -bound || x0 > bound ||
       x1 < -bound || x1 > bound || y0 < -bound || y0 > bound || y1 < -bound || y1 > bound) return false;
    // Endpoints and the +1-pixel brush are now safely inside the int range;
    // the bounded error/span arithmetic below cannot overflow either.
    int dx=int(wide_dx),sx=x0<x1?1:-1,dy=-int(wide_dy),sy=y0<y1?1:-1,err=dx+dy;
    for(;;) {
        pixel(x0,y0);pixel(x0+1,y0);pixel(x0,y0+1);
        if(x0==x1 && y0==y1) return true;
        int e=2*err; if(e>=dy) { err+=dy;x0+=sx; } if(e<=dx) { err+=dx;y0+=sy; }
    }
}
}
