// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "Protocol.hpp"
#include "fargan_cpu.h"

class FarganVocoder:public StreamNode {
public:
    enum selector {selAdvance,selStep};static std::array<uint16_t,2>selectors;
    FarganVocoder(EventQueue*q,const FarganVocoderParams&p):next_(q),step_(q),stats_(p.hw_.stats){}
    void subscribe(int port,StreamNode&dst,int dp) final {if(!port)next_.subscribe(dst,dp);else if(port==1)step_.subscribe(dst,dp);}
    cg_status processEvent(int port,Event&&e) final {
        if(!port&&e.event_id==selectors[selAdvance]&&e.wellFormed<uint64_t>()){
            if(work_)return CG_BUFFER_ERROR;
            work_=rvtts::work(e);work_->pcm.reset();work_->audio_frames=work_->frames+1;
            writer_.reset();frame_=0;minimum_=0xffffffffu;maximum_=0;
            stats_->generated_blocks=stats_->pcm_queue_max=stats_->pcm_queue_waits=0;
            stats_->pcm_hash=stats_->played_hash=stats_->generation_done_ticks=0;
            const uint32_t before=osKernelGetSysTimerCount();prefix_=before-work_->started;
            rvtts_fargan_cpu_prime(&state_,work_->features);
            active_=osKernelGetSysTimerCount()-before;stats_->vocoder_init_ticks=uint32_t(active_);
            // Publish the queue before generating the utterance. The next event
            // returns control to CMSIS Stream, allowing UI events between frames.
            if(!next_.sendAsync(kNormalPriority,selectors[selAdvance],rvtts::address(work_)))return CG_BUFFER_ERROR;
            return schedule();
        }
        if(port!=1||e.event_id!=selectors[selStep]||!work_)return CG_BUFFER_ERROR;
        auto*w=work_;
        if(w->pcm.full()){
            ++stats_->pcm_queue_waits;
            osDelay(1); // Backpressure; do not spin or block the audio thread.
            return schedule();
        }
        const uint32_t before=osKernelGetSysTimerCount();
        float pcm[160];int16_t converted[160];
        rvtts_fargan_cpu_frame(&state_,w->features+std::min(frame_,w->frames-1)*20,pcm);
        const unsigned first=frame_?0:80,last=frame_==w->frames+1?80:160;
        for(unsigned i=first;i<last;++i){
            uint32_t bits;std::memcpy(&bits,pcm+i,4);
            if((bits&0x7f800000u)==0x7f800000u)return CG_BUFFER_ERROR;
            float value=std::clamp(pcm[i]*32768.0f,-32767.0f,32767.0f);
            converted[i-first]=static_cast<int16_t>(std::floor(value+.5f));
        }
        if(!writer_.append(w->pcm,converted,last-first))return CG_BUFFER_ERROR;
        const uint32_t elapsed=osKernelGetSysTimerCount()-before;
        active_+=elapsed;minimum_=std::min(minimum_,elapsed);maximum_=std::max(maximum_,elapsed);
        stats_->generated_blocks=writer_.samples/160;stats_->pcm_queue_max=w->pcm.highWater();
        if(++frame_<w->frames+2)return schedule();
        if(!writer_.complete()||writer_.samples!=w->audio_frames*160)return CG_BUFFER_ERROR;
        stats_->generation_done_ticks=osKernelGetSysTimerCount()-w->started;
        stats_->vocoder_frames=frame_;stats_->vocoder_frame_ticks_min=minimum_;stats_->vocoder_frame_ticks_max=maximum_;
        stats_->vocoder_ticks64=active_;stats_->synthesis_ticks64=prefix_+active_;
        stats_->vocoder_ticks=uint32_t(stats_->vocoder_ticks64);stats_->synthesis_ticks=uint32_t(stats_->synthesis_ticks64);
        stats_->pcm_peak=writer_.peak;stats_->pcm_nonzero=writer_.nonzero;stats_->pcm_hash=writer_.hash;
        printf("READY generation=%lu frames=%lu audio_frames=%lu timer_hz=%lu frontend=%lu neural=%lu pitch=%lu vocoder=%llu total=%llu init=%lu frame_min=%lu frame_max=%lu peak=%lu nonzero=%lu done=%lu hash=%08lx queue=%lu waits=%lu\r\n",
            (unsigned long)w->generation,(unsigned long)w->frames,(unsigned long)w->audio_frames,(unsigned long)osKernelGetSysTimerFreq(),
            (unsigned long)stats_->frontend_ticks,(unsigned long)stats_->neural_ticks,(unsigned long)stats_->pitch_ticks,
            (unsigned long long)stats_->vocoder_ticks64,(unsigned long long)stats_->synthesis_ticks64,
            (unsigned long)stats_->vocoder_init_ticks,(unsigned long)minimum_,(unsigned long)maximum_,
            (unsigned long)stats_->pcm_peak,(unsigned long)stats_->pcm_nonzero,
            (unsigned long)stats_->generation_done_ticks,(unsigned long)stats_->pcm_hash,
            (unsigned long)stats_->pcm_queue_max,(unsigned long)stats_->pcm_queue_waits);
        work_=nullptr;
        return CG_SUCCESS;
    }
private:
    cg_status schedule(){return step_.sendAsync(kNormalPriority,selectors[selStep])?CG_SUCCESS:CG_BUFFER_ERROR;}
    EventOutput next_,step_;StreamStats*stats_;RvttsFarganCpuState state_{};
    rvtts::Work*work_=nullptr;rvtts::PcmWriter writer_;
    unsigned frame_=0;uint32_t prefix_=0,minimum_=0,maximum_=0;uint64_t active_=0;
};
