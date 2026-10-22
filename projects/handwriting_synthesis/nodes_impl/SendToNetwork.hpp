// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "../../../common/nodes_impl/c/NodeSupport.hpp"
#include "EventQueue.hpp"
#include "handwriting_protocol.hpp"
template<typename IN,int n> class SendToNetwork:public GenericSink<IN,n> {
public:
    enum selector { selRequest,selAck };
    static std::array<uint16_t,2> selectors;
    SendToNetwork(FIFOBase<IN>& in,EventQueue* q,const SendToNetworkParams& p)
        :GenericSink<IN,n>(in),requests_(q),stats_(p.hw_.stats) {}
    void subscribe(int port,StreamNode& dst,int dstPort) final { if(port==0) requests_.subscribe(dst,dstPort); }
    int run() final {
        const auto snapshot=*this->getReadBuffer();
        if(!snapshot.generation || busy_.load(std::memory_order_acquire) || done_.load()==snapshot.generation) return CG_SUCCESS;
        request_=snapshot; busy_.store(true,std::memory_order_release);
        if(!requests_.sendAsync(kNormalPriority,selectors[selRequest],uint64_t(reinterpret_cast<uintptr_t>(&request_)))) {
            ++stats_->event_drops; return CG_BUFFER_ERROR;
        }
        ++stats_->requests; return CG_SUCCESS;
    }
    cg_status processEvent(int port,Event&& evt) final {
        if(port || evt.event_id!=selectors[selAck] || !evt.wellFormed<uint32_t,uint32_t>()) return CG_BUFFER_ERROR;
        evt.apply<uint32_t,uint32_t>([&](uint32_t generation,uint32_t done) { if(done) done_.store(generation); });
        ++stats_->acknowledgments; busy_.store(false,std::memory_order_release); return CG_SUCCESS;
    }
private:
    EventOutput requests_; StreamStats* stats_; SentenceSnapshot request_{};
    std::atomic<bool> busy_{false}; std::atomic<uint32_t> done_{0};
};
