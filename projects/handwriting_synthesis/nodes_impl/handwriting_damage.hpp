// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
namespace handwriting {
// Damage is in logical landscape coordinates; storage is the rotated native
// 480-pixel stride. Only the owning display node tracks inter-buffer damage.
struct Damage {
    int left=800,top=480,right=-1,bottom=-1;
    bool empty() const { return right<left || bottom<top; }
    void include(int x,int y) {
        left=std::min(left,x);top=std::min(top,y);
        right=std::max(right,x);bottom=std::max(bottom,y);
    }
    void include(const Damage& other) {
        if(!other.empty()) { include(other.left,other.top);include(other.right,other.bottom); }
    }
    static Damage full() { return {0,0,799,479}; }
    unsigned native_x() const { return empty()?0:479-bottom; }
    unsigned native_y() const { return empty()?0:left; }
    unsigned native_width() const { return empty()?0:bottom-top+1; }
    unsigned native_height() const { return empty()?0:right-left+1; }
    size_t copy(uint16_t* destination,const uint16_t* source) const {
        size_t bytes=0;
        for(unsigned row=native_y();row<native_y()+native_height();++row) {
            size_t offset=row*480U+native_x(),count=native_width()*sizeof(uint16_t);
            std::memcpy(destination+offset,source+offset,count);bytes+=count;
        }
        return bytes;
    }
};
}
