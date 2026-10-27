// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "Protocol.hpp"
class LpcVocoder:public StreamNode {
public:
    enum selector {selAdvance,selStep};static std::array<uint16_t,2>selectors;
    LpcVocoder(EventQueue*q,const LpcVocoderParams&p):next_(q),step_(q),stats_(p.hw_.stats){}
    void subscribe(int port,StreamNode&dst,int dp) final {if(!port)next_.subscribe(dst,dp);else if(port==1)step_.subscribe(dst,dp);}
    cg_status processEvent(int port,Event&&e) final {
        if(!port&&e.event_id==selectors[selAdvance]&&e.wellFormed<uint64_t>()){
            if(work_)return CG_BUFFER_ERROR;
            work_=rvtts::work(e);work_->pcm.reset();work_->audio_frames=work_->frames;
            writer_.reset();frame_=0;
            stats_->generated_blocks=stats_->pcm_queue_max=stats_->pcm_queue_waits=0;
            stats_->pcm_hash=stats_->played_hash=stats_->generation_done_ticks=0;
            const uint32_t before=osKernelGetSysTimerCount();prefix_=before-work_->started;
            lpcvoc_init(&state_);active_=osKernelGetSysTimerCount()-before;
            if(!next_.sendAsync(kNormalPriority,selectors[selAdvance],rvtts::address(work_)))return CG_BUFFER_ERROR;
            return schedule();
        }
        if(port!=1||e.event_id!=selectors[selStep]||!work_)return CG_BUFFER_ERROR;
        auto*w=work_;
        if(w->pcm.full()){
            ++stats_->pcm_queue_waits;
            osDelay(1); // Backpressure; keep dataflow and other event handlers live.
            return schedule();
        }
        const uint32_t before=osKernelGetSysTimerCount();int16_t pcm[160];
        lpcvoc_frame(&state_,w->features+frame_*20,pcm);
        if(!writer_.append(w->pcm,pcm,160))return CG_BUFFER_ERROR;
        active_+=osKernelGetSysTimerCount()-before;
        stats_->generated_blocks=writer_.samples/160;stats_->pcm_queue_max=w->pcm.highWater();
        if(++frame_<w->frames)return schedule();
        if(!writer_.complete()||writer_.samples!=w->audio_frames*160)return CG_BUFFER_ERROR;
        stats_->generation_done_ticks=osKernelGetSysTimerCount()-w->started;
        stats_->vocoder_ticks=active_;stats_->synthesis_ticks=prefix_+active_;
        stats_->pcm_peak=writer_.peak;stats_->pcm_nonzero=writer_.nonzero;stats_->pcm_hash=writer_.hash;
        printf("READY generation=%lu frames=%lu timer_hz=%lu frontend=%lu neural=%lu pitch=%lu vocoder=%lu total=%lu peak=%lu nonzero=%lu done=%lu hash=%08lx queue=%lu waits=%lu\n",
            (unsigned long)w->generation,(unsigned long)w->frames,(unsigned long)osKernelGetSysTimerFreq(),
            (unsigned long)stats_->frontend_ticks,(unsigned long)stats_->neural_ticks,(unsigned long)stats_->pitch_ticks,
            (unsigned long)stats_->vocoder_ticks,(unsigned long)stats_->synthesis_ticks,
            (unsigned long)stats_->pcm_peak,(unsigned long)stats_->pcm_nonzero,
            (unsigned long)stats_->generation_done_ticks,(unsigned long)stats_->pcm_hash,
            (unsigned long)stats_->pcm_queue_max,(unsigned long)stats_->pcm_queue_waits);
        work_=nullptr;return CG_SUCCESS;
    }
private:
    cg_status schedule(){return step_.sendAsync(kNormalPriority,selectors[selStep])?CG_SUCCESS:CG_BUFFER_ERROR;}
    EventOutput next_,step_;StreamStats*stats_;LpcVocState state_{};
    rvtts::Work*work_=nullptr;rvtts::PcmWriter writer_;
    unsigned frame_=0;uint32_t prefix_=0,active_=0;
};
