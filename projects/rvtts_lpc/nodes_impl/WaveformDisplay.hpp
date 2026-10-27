// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "Protocol.hpp"
class WaveformDisplay:public StreamNode {
public:
    enum selector {selWave};static std::array<uint16_t,1>selectors;
    WaveformDisplay(const WaveformDisplayParams&p):hw_(p.hw_){}
    cg_status init() final;
    cg_status processEvent(int,Event&&) final;
private:HardwareParams hw_;static uint16_t buffers_[2][480*800];unsigned back_=1;
    static void pixel(uint16_t*b,int x,int y,uint16_t c){if(unsigned(x)<800&&unsigned(y)<480)b[(799-x)*480+y]=c;}
};
