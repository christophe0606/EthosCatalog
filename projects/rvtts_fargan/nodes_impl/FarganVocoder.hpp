// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "Protocol.hpp"
#include "fargan_cpu.h"

class FarganVocoder:public StreamNode {
public:
    enum selector {selAdvance};static std::array<uint16_t,1>selectors;
    FarganVocoder(EventQueue*q,const FarganVocoderParams&p):next_(q),stats_(p.hw_.stats){}
    void subscribe(int port,StreamNode&dst,int dp) final {if(!port)next_.subscribe(dst,dp);}
    cg_status processEvent(int port,Event&&e) final {
        if(port||e.event_id!=selectors[selAdvance]||!e.wellFormed<uint64_t>())return CG_BUFFER_ERROR;
        auto*w=rvtts::work(e);
        const uint32_t start=osKernelGetSysTimerCount(),start_tick=osKernelGetTickCount();
        rvtts_fargan_cpu_prime(&state_,w->features);
        const uint32_t primed=osKernelGetSysTimerCount();
        uint32_t minimum=0xffffffffu,maximum=0;
        unsigned written=0;
        for(unsigned t=0;t<w->frames+2;++t){
            const uint32_t before=osKernelGetSysTimerCount();
            float pcm[160];
            rvtts_fargan_cpu_frame(&state_,w->features+std::min(t,w->frames-1)*20,pcm);
            const unsigned first=t?0:80,last=t==w->frames+1?80:160;
            for(unsigned i=first;i<last;++i){
                uint32_t bits;std::memcpy(&bits,pcm+i,4);
                if((bits&0x7f800000u)==0x7f800000u)return CG_BUFFER_ERROR;
                float value=std::clamp(pcm[i]*32768.0f,-32767.0f,32767.0f);
                w->pcm[written++]=static_cast<int16_t>(std::floor(value+.5f));
            }
            const uint32_t elapsed=osKernelGetSysTimerCount()-before;
            minimum=std::min(minimum,elapsed);maximum=std::max(maximum,elapsed);
        }
        w->audio_frames=w->frames+1;
        if(written!=w->audio_frames*160)return CG_BUFFER_ERROR;
        stats_->vocoder_frames=w->frames+2;stats_->vocoder_frame_ticks_min=minimum;stats_->vocoder_frame_ticks_max=maximum;
        stats_->vocoder_init_ticks=primed-start;
        stats_->vocoder_ticks64=elapsedTicks(start,start_tick);
        stats_->synthesis_ticks64=elapsedTicks(w->started,w->started_tick);
        stats_->vocoder_ticks=uint32_t(stats_->vocoder_ticks64);stats_->synthesis_ticks=uint32_t(stats_->synthesis_ticks64);
        stats_->pcm_peak=0;stats_->pcm_nonzero=0;
        for(unsigned i=0;i<written;++i){uint32_t value=unsigned(std::abs(int(w->pcm[i])));if(value>stats_->pcm_peak)stats_->pcm_peak=value;if(value)++stats_->pcm_nonzero;}
        printf("READY generation=%lu frames=%lu audio_frames=%lu timer_hz=%lu frontend=%lu neural=%lu pitch=%lu vocoder=%llu total=%llu init=%lu frame_min=%lu frame_max=%lu peak=%lu nonzero=%lu\n",
            (unsigned long)w->generation,(unsigned long)w->frames,(unsigned long)w->audio_frames,(unsigned long)osKernelGetSysTimerFreq(),
            (unsigned long)stats_->frontend_ticks,(unsigned long)stats_->neural_ticks,(unsigned long)stats_->pitch_ticks,
            (unsigned long long)stats_->vocoder_ticks64,(unsigned long long)stats_->synthesis_ticks64,
            (unsigned long)stats_->vocoder_init_ticks,(unsigned long)minimum,(unsigned long)maximum,
            (unsigned long)stats_->pcm_peak,(unsigned long)stats_->pcm_nonzero);
        return next_.sendAsync(kNormalPriority,selectors[selAdvance],rvtts::address(w))?CG_SUCCESS:CG_BUFFER_ERROR;
    }
private:
    static uint64_t elapsedTicks(uint32_t start,uint32_t tick){
        const uint64_t fine=uint32_t(osKernelGetSysTimerCount()-start);
        const uint64_t coarse=uint64_t(uint32_t(osKernelGetTickCount()-tick))*osKernelGetSysTimerFreq()/osKernelGetTickFreq();
        const uint64_t wraps=coarse>fine?(coarse-fine+(1ull<<31))>>32:0;
        return fine+(wraps<<32);
    }
    EventOutput next_;StreamStats*stats_;RvttsFarganCpuState state_{};
};
