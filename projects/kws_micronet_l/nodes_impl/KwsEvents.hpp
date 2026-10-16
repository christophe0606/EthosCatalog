// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "../../../common/nodes_impl/c/NodeSupport.hpp"
#include "EventQueue.hpp"
template<typename IN,int inputSamples>
class KwsEvents : public GenericSink<IN,inputSamples> {
public:
    enum selector {selTick=0};
    static std::array<uint16_t,1> selectors;
    KwsEvents(FIFOBase<IN>& in,EventQueue* queue,const KwsEventsParams& p)
        : GenericSink<IN,inputSamples>(in),events_(queue),p_(p) {}
    cg_status init() final { return p_.hw_.stats && p_.fps>0 ? CG_SUCCESS : CG_INIT_FAILURE; }
    void subscribe(int port,StreamNode& dst,int dstPort) final { if(port==0) events_.subscribe(dst,dstPort); }
    int run() final {
        (void)this->getReadBuffer();
        auto& s=*p_.hw_.stats;
        ++s.mfcc_frames;
        uint32_t now=osKernelGetTickCount();
        if(!sent_ || int32_t(now-next_)>=0) {
            if(events_.sendAsync(kNormalPriority,selectors[selTick],now)) ++s.lcd_events_sent;
            else ++s.lcd_events_dropped;
            uint32_t period=osKernelGetTickFreq()/p_.fps;
            next_=sent_ ? next_+period : now+period;
            if(int32_t(now-next_)>=0) next_=now+period;
            sent_=true;
        }
        uint32_t elapsed=osKernelGetSysTimerCount()-s.audio_started;
        if(elapsed>s.audio_ticks_max) s.audio_ticks_max=elapsed;
        if(elapsed>uint64_t(osKernelGetSysTimerFreq())*MIC_BLOCK_SAMPLES/MIC_SAMPLE_RATE) ++s.audio_deadline_misses;
        return CG_SUCCESS;
    }
private:
    EventOutput events_; KwsEventsParams p_; bool sent_=false; uint32_t next_=0;
};
