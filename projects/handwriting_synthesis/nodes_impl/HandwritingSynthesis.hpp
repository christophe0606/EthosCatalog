// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "../../../common/nodes_impl/c/NodeSupport.hpp"
#include "EventQueue.hpp"
#include "handwriting_protocol.hpp"
#include "handwriting_layout.hpp"
#include <cstdio>
#include "main.h"
class HandwritingSynthesis:public StreamNode {
public:
    enum selector { selRequest,selAck,selStroke,selAdvance };
    static std::array<uint16_t,4> selectors;
    HandwritingSynthesis(EventQueue* q,const HandwritingSynthesisParams& p)
        :acks_(q),results_(q),models_(q),stats_(p.hw_.stats),engine_(static_cast<arm::embedded::EmbeddedModule*>(p.hw_.module)) {}
    // oev0 acknowledges SendToNetwork, oev1 publishes to the display, and
    // oev2 starts the model chain. Its gmm output returns on iev1.
    void subscribe(int port,StreamNode& dst,int dstPort) final {
        if(port==0) acks_.subscribe(dst,dstPort); else if(port==1) results_.subscribe(dst,dstPort); else if(port==2) models_.subscribe(dst,dstPort);
    }
    cg_status processEvent(int port,Event&& evt) final {
        if(port==1) {
            if(!active_ || evt.event_id!=selectors[selAdvance] || !evt.wellFormed<uint64_t>() ||
               evt.get<uint64_t>()!=uint64_t(reinterpret_cast<uintptr_t>(&work_))) return CG_BUFFER_ERROR;
            active_=false;
            return finish();
        }
        if(active_) return CG_BUFFER_ERROR;
        if(port || evt.event_id!=selectors[selRequest] || !evt.wellFormed<uint64_t>()) return CG_BUFFER_ERROR;
        const auto request=*reinterpret_cast<const SentenceSnapshot*>(uintptr_t(evt.get<uint64_t>()));
        if(!handwriting::current(request)) return acknowledge(request.generation,false);
        if(generation_!=request.generation) {
            int error=engine_.begin(request.text,request.seed,request.bias); if(error) return static_cast<cg_status>(error);
            generation_=request.generation; result_={request,{},1,0,false};
            profile_count_=0;
            results_.sendSync(kNormalPriority,selectors[selStroke],uint64_t(reinterpret_cast<uintptr_t>(&result_)));
            // Stream's synchronous publisher has a void return type. The
            // display preserves a hardware failure in diagnostics for this check.
            if(stats_->error) return static_cast<cg_status>(stats_->error);
        }
        request_=request;
        work_={&engine_,{},0,0};
        ethos_benchmark_reset(); active_=true;
        if(!models_.sendAsync(kNormalPriority,selectors[selAdvance],uint64_t(reinterpret_cast<uintptr_t>(&work_)))) {
            active_=false; ++stats_->event_drops; return CG_BUFFER_ERROR;
        }
        return CG_SUCCESS;
    }
private:
    cg_status finish() {
        const auto& request=request_;
        const auto point=work_.point;
        const int error=work_.status;
        const uint32_t ticks=work_.ticks;
        stats_->forward_ticks_sum+=ticks;
        if(error!=2 && (ethos_benchmark.invocations!=5 || !ethos_benchmark.cycles ||
            ethos_benchmark.cycles!=ethos_benchmark.active_cycles || ethos_benchmark.overflow)) return static_cast<cg_status>(-434);
        // A bounded, one-shot profile for a full75-character input. The first
        // step is warmup. Sum model-node processing intervals, including CPU
        // recurrence/sampling; serial/LCD/input/queue time stays outside them.
        if(error==0 && std::strlen(request.text)==75 && engine_.state().steps>1 && profile_count_<64) {
            profile_cycles_[profile_count_]=ethos_benchmark.cycles;
            profile_ticks_[profile_count_++]=ticks;
            if(profile_count_==64) {
                printf("PROFILE generation=%lu samples=64 timer_hz=%lu OSPI_HZ=%lu\n",(unsigned long)generation_,(unsigned long)osKernelGetSysTimerFreq(),(unsigned long)APP_READ_CLOCK_HZ);
                printf("PROFILE_CYCLES");for(auto v:profile_cycles_) printf(" %llu",(unsigned long long)v);printf("\n");
                printf("PROFILE_TICKS");for(auto v:profile_ticks_) printf(" %lu",(unsigned long)v);printf("\n");
            }
        }
        if(ticks>stats_->forward_ticks_max) stats_->forward_ticks_max=ticks;
        ++stats_->inferences;
        if(error<0) return static_cast<cg_status>(error);
        if(!handwriting::current(request)) { ++stats_->stale_results; return acknowledge(request.generation,false); }
        if(error==0) ++stats_->generated; else if(error==1) ++stats_->completed; else ++stats_->step_limits;
        unsigned character=0; const auto& s=engine_.state();
        for(unsigned u=1;u<s.length;++u) if(s.phi[u]>s.phi[character]) character=u;
        const unsigned length=unsigned(std::strlen(request.text));
        // Attention may reach NUL while the final mark is still being drawn.
        // Retain its punctuation policy through those finishing strokes.
        bool punctuation=length && (handwriting::is_punctuation(request.text[std::min(character,length-1)]) ||
                                    handwriting::punctuation_suffix(request.text,character));
        bool word=character<length && request.text[character]==' ' && !punctuation;
        result_={request,point,error==0?0U:error==1?2U:3U,character,word,punctuation};
        if(!results_.sendAsync(kNormalPriority,selectors[selStroke],uint64_t(reinterpret_cast<uintptr_t>(&result_)))) {
            ++stats_->event_drops; return CG_BUFFER_ERROR;
        }
        return acknowledge(request.generation,error!=0);
    }
    cg_status acknowledge(uint32_t generation,bool done) {
        if(!acks_.sendAsync(kNormalPriority,selectors[selAck],uint32_t(generation),uint32_t(done))) {
            ++stats_->event_drops; return CG_BUFFER_ERROR;
        }
        return CG_SUCCESS;
    }
    EventOutput acks_,results_,models_; StreamStats* stats_;
    handwriting::Engine engine_; handwriting::Result result_{}; uint32_t generation_=0;
    handwriting::Work work_{}; SentenceSnapshot request_{}; bool active_=false;
    uint64_t profile_cycles_[64]{}; uint32_t profile_ticks_[64]{},profile_count_=0;
};
