// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "../../../common/nodes_impl/c/NodeSupport.hpp"
#include "EventQueue.hpp"
#include "handwriting_protocol.hpp"

// Reused by lstm1, attention, lstm2, lstm3 and gmm. Each instance executes
// exactly its configured PTE method; Work belongs to the synthesis controller.
class HandwritingNetwork:public StreamNode {
public:
    enum selector { selAdvance };
    static std::array<uint16_t,1> selectors;
    HandwritingNetwork(EventQueue* queue,const HandwritingNetworkParams& params)
        :next_(queue),stats_(params.hw_.stats),method_(params.method) {}
    cg_status init() final { return method_<handwriting::MethodCount?CG_SUCCESS:CG_BUFFER_ERROR; }
    void subscribe(int port,StreamNode& destination,int destinationPort) final {
        if(port==0) next_.subscribe(destination,destinationPort);
    }
    cg_status processEvent(int port,Event&& event) final {
        if(port || event.event_id!=selectors[selAdvance] || !event.wellFormed<uint64_t>()) return CG_BUFFER_ERROR;
        auto* work=reinterpret_cast<handwriting::Work*>(uintptr_t(event.get<uint64_t>()));
        if(!work || !work->engine || method_>=handwriting::MethodCount) return CG_BUFFER_ERROR;
        if(work->status==0) {
            const uint32_t start=osKernelGetSysTimerCount();
            work->status=work->engine->stage(method_,work->point);
            work->ticks+=osKernelGetSysTimerCount()-start;
            if(work->status<0) return static_cast<cg_status>(work->status);
        }
        if(!next_.sendAsync(kNormalPriority,selectors[selAdvance],uint64_t(reinterpret_cast<uintptr_t>(work)))) {
            ++stats_->event_drops; return CG_BUFFER_ERROR;
        }
        return CG_SUCCESS;
    }
private:
    EventOutput next_;
    StreamStats* stats_;
    unsigned method_;
};
