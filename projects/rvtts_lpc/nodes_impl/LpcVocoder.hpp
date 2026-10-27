// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "Protocol.hpp"
class LpcVocoder:public StreamNode {
public:
    enum selector {selAdvance};static std::array<uint16_t,1>selectors;
    LpcVocoder(EventQueue*q,const LpcVocoderParams&p):next_(q),stats_(p.hw_.stats){}
    void subscribe(int port,StreamNode&dst,int dp) final {if(!port)next_.subscribe(dst,dp);}
    cg_status processEvent(int port,Event&&e) final {
        if(port||e.event_id!=selectors[selAdvance]||!e.wellFormed<uint64_t>())return CG_BUFFER_ERROR;
        auto*w=rvtts::work(e);auto start=osKernelGetSysTimerCount();lpcvoc_init(&state_);
        for(unsigned t=0;t<w->frames;++t)lpcvoc_frame(&state_,w->features+t*20,w->pcm+t*160);
        stats_->vocoder_ticks=osKernelGetSysTimerCount()-start;stats_->synthesis_ticks=osKernelGetSysTimerCount()-w->started;
        stats_->pcm_peak=0;stats_->pcm_nonzero=0;for(unsigned i=0;i<w->frames*160;++i){uint32_t v=unsigned(std::abs(int(w->pcm[i])));if(v>stats_->pcm_peak)stats_->pcm_peak=v;if(v)++stats_->pcm_nonzero;}
        printf("READY generation=%lu frames=%lu timer_hz=%lu frontend=%lu neural=%lu pitch=%lu vocoder=%lu total=%lu peak=%lu nonzero=%lu\n",(unsigned long)w->generation,(unsigned long)w->frames,(unsigned long)osKernelGetSysTimerFreq(),(unsigned long)stats_->frontend_ticks,(unsigned long)stats_->neural_ticks,(unsigned long)stats_->pitch_ticks,(unsigned long)stats_->vocoder_ticks,(unsigned long)stats_->synthesis_ticks,(unsigned long)stats_->pcm_peak,(unsigned long)stats_->pcm_nonzero);
        return next_.sendAsync(kNormalPriority,selectors[selAdvance],rvtts::address(w))?CG_SUCCESS:CG_BUFFER_ERROR;
    }
private:EventOutput next_;StreamStats*stats_;LpcVocState state_{};
};
