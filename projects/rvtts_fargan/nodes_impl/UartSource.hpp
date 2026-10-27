// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "Protocol.hpp"
template<typename OUT,int n> class UartSource:public GenericSource<OUT,n> {
public:
    enum selector {selAdvance}; static std::array<uint16_t,1> selectors;
    UartSource(FIFOBase<OUT>&o,const UartSourceParams&p):GenericSource<OUT,n>(o),hw_(p.hw_){}
    int run() final {auto& b=*this->getWriteBuffer();b.count=0;for(unsigned i=0;i<32;++i){int c=hw_.uart_read();if(c<0)break;b.bytes[b.count++]=uint8_t(c);++hw_.stats->uart_bytes;}return CG_SUCCESS;}
private: HardwareParams hw_;
};
