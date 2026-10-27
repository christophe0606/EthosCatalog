// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "Protocol.hpp"
class PitchCorrect:public StreamNode {
public:
    enum selector {selAdvance};static std::array<uint16_t,1>selectors;
    PitchCorrect(EventQueue*q,const PitchCorrectParams&p):next_(q),stats_(p.hw_.stats){}
    void subscribe(int port,StreamNode&dst,int dp) final {if(!port)next_.subscribe(dst,dp);}
    cg_status processEvent(int port,Event&&e) final {
        if(port||e.event_id!=selectors[selAdvance]||!e.wellFormed<uint64_t>())return CG_BUFFER_ERROR;
        auto*w=rvtts::work(e);auto start=osKernelGetSysTimerCount();double sum=0,sq=0;
        for(unsigned t=0;t<w->frames;++t){float p=w->features[t*20+18];sum+=p;sq+=double(p)*p;}
        float mean=float(sum/w->frames);double variance=sq/w->frames-double(mean)*mean;float sd=variance>1e-10?float(std::sqrt(variance)):1e-5f;
        for(unsigned t=0;t<w->frames;++t){float& p=w->features[t*20+18];p=std::clamp((p-mean)/sd,-2.5f,2.5f)*.3615f+.2120f;}
        stats_->pitch_ticks=osKernelGetSysTimerCount()-start;
        return next_.sendAsync(kNormalPriority,selectors[selAdvance],rvtts::address(w))?CG_SUCCESS:CG_BUFFER_ERROR;
    }
private:EventOutput next_;StreamStats*stats_;
};
