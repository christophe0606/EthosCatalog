// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "Protocol.hpp"
class TextFrontend:public StreamNode {
public:
    enum selector {selRequest,selAdvance,selAck};static std::array<uint16_t,3>selectors;
    TextFrontend(EventQueue*q,const TextFrontendParams&p):next_(q),ack_(q),stats_(p.hw_.stats){}
    void subscribe(int port,StreamNode&dst,int dp) final {if(!port)next_.subscribe(dst,dp);else if(port==1)ack_.subscribe(dst,dp);}
    cg_status processEvent(int port,Event&&e) final {
        if(port||e.event_id!=selectors[selRequest]||!e.wellFormed<uint64_t>())return CG_BUFFER_ERROR;
        const auto& l=*reinterpret_cast<TextLine*>(uintptr_t(e.get<uint64_t>()));
        work_.started=osKernelGetSysTimerCount();work_.generation=l.generation;
        stats_->utterance_started=work_.started;
        int error=tts_frontend_process(l.text,&work_.frontend,0);work_.frames=unsigned(work_.frontend.frame_count);
        if(error||!work_.frames||work_.frames>rvtts::MaxFrames){++stats_->rejected;
            printf("REJECT frontend=%d frames=%lu maximum=%u; shorten the sentence\r\n",error,(unsigned long)work_.frames,rvtts::MaxFrames);
            return ack_.sendAsync(kNormalPriority,selectors[selAck],l.generation)?CG_SUCCESS:CG_BUFFER_ERROR;}
        word_vocab_assign_frames_indexed(l.text,work_.frontend.frame_word,work_.frames,work_.words);
        stats_->frontend_ticks=osKernelGetSysTimerCount()-work_.started;stats_->neural_ticks=0;stats_->frames=work_.frames;
        printf("SYNTH generation=%lu frames=%lu text=\"%s\"\r\n",(unsigned long)l.generation,(unsigned long)work_.frames,l.text);
        return next_.sendAsync(kNormalPriority,selectors[selAdvance],rvtts::address(&work_))?CG_SUCCESS:CG_BUFFER_ERROR;
    }
private:EventOutput next_,ack_;StreamStats*stats_;static rvtts::Work work_;
};
