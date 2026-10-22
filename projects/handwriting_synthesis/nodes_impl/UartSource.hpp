// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "../../../common/nodes_impl/c/NodeSupport.hpp"
template<typename OUT,int n> class UartSource: public GenericSource<OUT,n> {
public:
    UartSource(FIFOBase<OUT>& out,const UartSourceParams& p):GenericSource<OUT,n>(out),hw_(p.hw_) {}
    int run() final {
        osDelay(std::max(1U,osKernelGetTickFreq()/200U));
        uint32_t start=osKernelGetSysTimerCount();
        auto& b=*this->getWriteBuffer(); b.count=0;
        while(b.count<32) { int c=hw_.uart_read(); if(c<0) break; b.bytes[b.count++]=uint8_t(c); }
        hw_.stats->uart_bytes+=b.count;
        uint32_t ticks=osKernelGetSysTimerCount()-start;
        if(ticks>hw_.stats->source_ticks_max) hw_.stats->source_ticks_max=ticks;
        if(ticks>osKernelGetSysTimerFreq()/200) ++hw_.stats->source_deadline_misses;
        return CG_SUCCESS;
    }
private: HardwareParams hw_;
};
