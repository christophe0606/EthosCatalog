// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "Protocol.hpp"
template<typename IN,int ni,typename OUT,int n> class PlaybackSource:public GenericNode<IN,ni,OUT,n> {
public:
    enum selector {selAdvance,selAck};static std::array<uint16_t,2>selectors;
    PlaybackSource(FIFOBase<IN>&i,FIFOBase<OUT>&o,EventQueue*q,const PlaybackSourceParams&p):GenericNode<IN,ni,OUT,n>(i,o),ack_(q),stats_(p.hw_.stats){}
    void subscribe(int port,StreamNode&dst,int dp) final {if(!port)ack_.subscribe(dst,dp);}
    cg_status processEvent(int port,Event&&e) final {if(port||e.event_id!=selectors[selAdvance]||!e.wellFormed<uint64_t>())return CG_BUFFER_ERROR;pending_.store(rvtts::work(e),std::memory_order_release);return CG_SUCCESS;}
    int run() final {
        (void)this->getReadBuffer();auto& b=*this->getWriteBuffer();b={};
        if(!work_){work_=pending_.exchange(nullptr,std::memory_order_acq_rel);position_=0;}
        if(work_){std::memcpy(b.samples,work_->pcm+position_*160,320);b.generation=work_->generation;b.position=position_;b.total=work_->audio_frames;
            if(++position_==work_->audio_frames){uint32_t gen=work_->generation;work_=nullptr;if(!ack_.sendAsync(kNormalPriority,selectors[selAck],gen))return CG_BUFFER_ERROR;}}
        return CG_SUCCESS;
    }
private:EventOutput ack_;StreamStats*stats_;std::atomic<rvtts::Work*>pending_{nullptr};rvtts::Work*work_=nullptr;unsigned position_=0;
};
