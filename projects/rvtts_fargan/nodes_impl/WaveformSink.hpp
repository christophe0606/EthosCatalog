// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "Protocol.hpp"
template<typename IN,int n> class WaveformSink:public GenericSink<IN,n> {
public:
    enum selector {selWave};static std::array<uint16_t,1>selectors;
    WaveformSink(FIFOBase<IN>&i,EventQueue*q,const WaveformSinkParams&p):GenericSink<IN,n>(i),next_(q),stats_(p.hw_.stats){}
    void subscribe(int port,StreamNode&dst,int dp) final {if(!port)next_.subscribe(dst,dp);}
    int run() final {const auto& b=*this->getReadBuffer();if(++blocks_%5)return CG_SUCCESS;
        if(snapshot_.pending.exchange(true,std::memory_order_acq_rel)){++stats_->lcd_skipped;return CG_SUCCESS;}
        std::memcpy(snapshot_.samples,b.samples,320);snapshot_.position=b.position;snapshot_.total=b.total;
        if(!next_.sendAsync(kNormalPriority,selectors[selWave],rvtts::address(&snapshot_))){snapshot_.pending.store(false);++stats_->event_drops;return CG_BUFFER_ERROR;}return CG_SUCCESS;}
private:EventOutput next_;StreamStats*stats_;rvtts::Waveform snapshot_;unsigned blocks_=0;
};
