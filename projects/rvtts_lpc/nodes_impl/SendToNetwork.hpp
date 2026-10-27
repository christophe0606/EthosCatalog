// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "Protocol.hpp"
template<typename IN,int n,typename OUT,int no> class SendToNetwork:public GenericNode<IN,n,OUT,no> {
public:
    enum selector {selRequest,selAck};static std::array<uint16_t,2> selectors;
    SendToNetwork(FIFOBase<IN>&i,FIFOBase<OUT>&o,EventQueue*q,const SendToNetworkParams&p):GenericNode<IN,n,OUT,no>(i,o),request_(q),stats_(p.hw_.stats){}
    void subscribe(int port,StreamNode&dst,int dp) final {if(!port)request_.subscribe(dst,dp);}
    int run() final {*this->getWriteBuffer()=0;const auto& l=*this->getReadBuffer();if(!l.generation||l.generation==done_||busy_.load(std::memory_order_acquire))return CG_SUCCESS;
        line_=l;busy_.store(true,std::memory_order_release);++stats_->requests;
        if(!request_.sendAsync(kNormalPriority,selectors[selRequest],rvtts::address(&line_))){++stats_->event_drops;return CG_BUFFER_ERROR;}return CG_SUCCESS;}
    cg_status processEvent(int port,Event&&e) final {if(port||e.event_id!=selectors[selAck]||!e.wellFormed<uint32_t>())return CG_BUFFER_ERROR;
        done_=e.get<uint32_t>();++stats_->acknowledgments;busy_.store(false,std::memory_order_release);return CG_SUCCESS;}
private:EventOutput request_;StreamStats*stats_;TextLine line_{};std::atomic<bool>busy_{false};std::atomic<uint32_t>done_{0};
};
