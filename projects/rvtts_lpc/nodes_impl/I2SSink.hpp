// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "Protocol.hpp"
template<typename IN,int n> class I2SSink:public GenericSink<IN,n> {
public:
    enum selector {selAdvance};static std::array<uint16_t,1>selectors;
    I2SSink(FIFOBase<IN>&i,const I2SSinkParams&p):GenericSink<IN,n>(i),hw_(p.hw_){}
    int run() final {const auto& b=*this->getReadBuffer();int e=hw_.audio_write(b.samples,b.generation);if(e)return e;if(b.generation)++hw_.stats->played_blocks;return CG_SUCCESS;}
private:HardwareParams hw_;
};
