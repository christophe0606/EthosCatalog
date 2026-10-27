// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "Protocol.hpp"
class RvttsNetwork:public StreamNode {
public:
    enum selector {selAdvance};static std::array<uint16_t,1>selectors;
    RvttsNetwork(EventQueue*q,const RvttsNetworkParams&p):next_(q),stats_(p.hw_.stats),method_(p.method){}
    void subscribe(int port,StreamNode&dst,int dp) final {if(!port)next_.subscribe(dst,dp);}
    cg_status processEvent(int port,Event&&e) final {
        if(port||e.event_id!=selectors[selAdvance]||!e.wellFormed<uint64_t>())return CG_BUFFER_ERROR;
        auto*w=rvtts::work(e);rvtts::Timing t{};int error=rvtts::feature_stage(method_,w->frontend.frame_phones,w->frontend.frame_stress,w->words,w->frames,w->features,&t);
        stats_->neural_ticks+=t.ticks;if(error)return static_cast<cg_status>(error);
        return next_.sendAsync(kNormalPriority,selectors[selAdvance],rvtts::address(w))?CG_SUCCESS:CG_BUFFER_ERROR;
    }
private:EventOutput next_;StreamStats*stats_;unsigned method_;
};
