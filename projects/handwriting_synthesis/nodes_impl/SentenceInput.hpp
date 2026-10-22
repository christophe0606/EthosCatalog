// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "../../../common/nodes_impl/c/NodeSupport.hpp"
#include "handwriting_protocol.hpp"
#include <cstdio>
// Editor owns accepted sentence and generation. A busy network cannot lose
// the latest replacement: its snapshot is republished on every firing.
template<typename IN,int ni,typename OUT,int no>
class SentenceInput:public GenericNode<IN,ni,OUT,no> {
public:
    SentenceInput(FIFOBase<IN>& in,FIFOBase<OUT>& out,const SentenceInputParams& p)
        :GenericNode<IN,ni,OUT,no>(in,out),stats_(p.hw_.stats) { accepted_.current=&generation_; }
    int run() final {
        const auto& b=*this->getReadBuffer();
        for(unsigned i=0;i<b.count;++i) consume(b.bytes[i]);
        *this->getWriteBuffer()=accepted_; return CG_SUCCESS;
    }
private:
    void consume(unsigned c) {
        if(c=='\n' && cr_) { cr_=false; return; }
        cr_=c=='\r';
        if(c=='\r' || c=='\n') {
            if(invalid_ || excess_) { ++stats_->rejected; printf("Input rejected: unsupported character or more than 75 characters.\n"); }
            else if(length_) {
                std::memcpy(accepted_.text,line_,length_); accepted_.text[length_]=0;
                accepted_.generation=generation_.load(std::memory_order_relaxed)+1;
                generation_.store(accepted_.generation,std::memory_order_release);
                stats_->generation=accepted_.generation; ++stats_->sentences;
                printf("Sentence %lu (%u chars): %s\n",(unsigned long)accepted_.generation,length_,accepted_.text);
            }
            length_=excess_=invalid_=0; return;
        }
        if(c==8 || c==127) {
            if(excess_) --excess_;
            else if(length_) { unsigned removed=uint8_t(line_[--length_]); if(!supported(removed)) --invalid_; }
            return;
        }
        if(c==21 || c==27) { length_=excess_=invalid_=0; return; }
        if(c=='Q' || c=='X' || c=='Z') { c+=32; ++stats_->normalized; }
        if(length_==75) { ++excess_; return; }
        if(!supported(c)) ++invalid_;
        line_[length_++]=char(c);
    }
    static bool supported(unsigned c) { return c && c<=126 && std::strchr(handwriting::alphabet+1,int(c)); }
    StreamStats* stats_; char line_[76]{}; unsigned length_=0,excess_=0,invalid_=0;
    bool cr_=false; std::atomic<uint32_t> generation_{0}; SentenceSnapshot accepted_{};
};
