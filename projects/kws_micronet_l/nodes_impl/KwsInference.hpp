// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "../../../common/nodes_impl/c/NodeSupport.hpp"
#include "embedded_kws.h"
#include "EventQueue.hpp"
template<typename IN,int inputSamples>
class KwsInference : public GenericSink<IN,inputSamples> {
public:
    enum selector {selKeyword=0};
    static std::array<uint16_t,1> selectors;
    KwsInference(FIFOBase<IN>& in,EventQueue* queue,const KwsInferenceParams& p)
        : GenericSink<IN,inputSamples>(in),events_(queue),p_(p) { static_assert(inputSamples==490); }
    cg_status init() final { return p_.hw_.module && p_.hw_.stats ? CG_SUCCESS : CG_INIT_FAILURE; }
    void subscribe(int port,StreamNode& dst,int dstPort) final { if(port==0) events_.subscribe(dst,dstPort); }
    int run() final {
        auto *features=this->getReadBuffer();
        auto& s=*p_.hw_.stats;
        // Discard the zero-padded startup frame; require a full real context,
        // also after a capture discontinuity.
        constexpr unsigned fresh_blocks = KWS_CONTEXT_FRAMES + (KWS_FRAME_SAMPLES-1)/MIC_BLOCK_SAMPLES;
        if (s.audio_blocks-s.last_discontinuity_block < fresh_blocks) { ++s.warmup_windows; return CG_SUCCESS; }
        int8_t scores[12]; uint32_t ticks;
        int error=kws_forward(p_.hw_.module,features,scores,&ticks);
        if(error) return error;
        ++s.inferences;
        if(ticks>s.inference_ticks_max) s.inference_ticks_max=ticks;
        float probability;
        unsigned best=kws_classify(scores,&probability);
        s.last_class=best; s.last_probability=probability;
        if (best<10 && probability>=p_.threshold) {
            ++s.recognitions;
            if(events_.sendAsync(kNormalPriority,selectors[selKeyword],uint32_t(best),osKernelGetTickCount())) ++s.lcd_events_sent;
            else ++s.lcd_events_dropped;
        }
        uint32_t elapsed=osKernelGetSysTimerCount()-s.audio_started;
        if(elapsed>s.audio_ticks_max) s.audio_ticks_max=elapsed;
        if(elapsed>uint64_t(osKernelGetSysTimerFreq())*MIC_BLOCK_SAMPLES/MIC_SAMPLE_RATE) ++s.audio_deadline_misses;
        return CG_SUCCESS;
    }
private:
    EventOutput events_;
    KwsInferenceParams p_;
};
