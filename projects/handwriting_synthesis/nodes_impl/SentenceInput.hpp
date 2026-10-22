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
        // A partial line must reach the terminal before Return is pressed.
        if(b.count) std::fflush(stdout);
        *this->getWriteBuffer()=accepted_; return CG_SUCCESS;
    }
private:
    void consume(unsigned c) {
        if(c=='\n' && cr_) { cr_=false; return; }
        cr_=c=='\r';
        if(c=='\r' || c=='\n') {
            std::fputs("\r\n",stdout);
            if(invalid_ || excess_) { ++stats_->rejected; printf("Input rejected: unsupported character or more than 75 characters.\r\n"); }
            else if(length_) {
                line_[length_]=0;
                if(!command()) {
                    // A doubled colon escapes the command prefix in literal text.
                    const unsigned offset=length_>1 && line_[0]==':' && line_[1]==':'?1:0;
                    std::memcpy(accepted_.text,line_+offset,length_-offset); accepted_.text[length_-offset]=0;
                    accepted_.bias=bias_; accepted_.seed=seed_;
                    accepted_.generation=generation_.load(std::memory_order_relaxed)+1;
                    generation_.store(accepted_.generation,std::memory_order_release);
                    stats_->generation=accepted_.generation; ++stats_->sentences;
                    printf("Sentence %lu (%u chars): %s\r\n",(unsigned long)accepted_.generation,length_-offset,accepted_.text);
                }
            }
            length_=excess_=invalid_=0; return;
        }
        if(c==8 || c==127) {
            if(length_ || excess_) erase(1);
            if(excess_) --excess_;
            else if(length_) { unsigned removed=uint8_t(line_[--length_]); if(!supported(removed)) --invalid_; }
            return;
        }
        if(c==21 || c==27) { erase(length_+excess_); length_=excess_=invalid_=0; return; }
        if(c=='Q' || c=='X' || c=='Z') { c+=32; ++stats_->normalized; }
        // Reset the column after diagnostic lines, whose LF may not imply CR.
        if(!length_ && !excess_) std::putchar('\r');
        // Display invalid control/non-ASCII bytes without executing them in
        // the terminal. Each byte occupies one cell and remains correctable.
        std::putchar(c>=32 && c<=126?int(c):'?');
        if(length_==75) { ++excess_; return; }
        if(!supported(c)) ++invalid_;
        line_[length_++]=char(c);
    }
    static void erase(unsigned count) { while(count--) std::fputs("\b \b",stdout); }
    static bool supported(unsigned c) { return c && c<=126 && std::strchr(handwriting::alphabet+1,int(c)); }
    static const char* argument(const char* line,const char* name) {
        const auto n=std::strlen(name);
        if(std::strncmp(line,name,n) || (line[n] && line[n]!=' ')) return nullptr;
        line+=n;while(*line==' ') ++line;return line;
    }
    static bool end(const char* text) { while(*text==' ') ++text;return !*text; }
    bool command() {
        if(line_[0]!=':' || line_[1]==':') return false;
        const char* value=argument(line_,":bias");
        if(value) {
            if(*value) {
                // Parse a finite decimal directly; reject signs, NaN, infinity
                // and overflow without relying on fast-math isfinite checks.
                float candidate=0,fraction=.1f;unsigned digits=0;
                while(*value>='0' && *value<='9') {
                    candidate=candidate*10+(*value++-'0');++digits;
                    if(candidate>2) { command_error();return true; }
                }
                if(*value=='.') {
                    ++value;
                    while(*value>='0' && *value<='9') {
                        candidate+=(*value++-'0')*fraction;fraction*=.1f;++digits;
                    }
                }
                if(!digits || !end(value) || candidate>2) { command_error();return true; }
                bias_=candidate;
            }
        } else if((value=argument(line_,":seed"))) {
            if(*value) {
                uint32_t candidate=0;unsigned digits=0;
                while(*value>='0' && *value<='9') {
                    const unsigned digit=*value++-'0';++digits;
                    if(candidate>(UINT32_MAX-digit)/10U) { command_error();return true; }
                    candidate=candidate*10U+digit;
                }
                if(!digits || !candidate || !end(value)) { command_error();return true; }
                seed_=candidate;
            }
        } else {
            if(std::strcmp(line_,":help")) ++stats_->rejected;
            printf("Commands: :bias 0..2, :seed 1..4294967295, :help. Use :: for literal leading colon.\r\n");
            return true;
        }
        printf("Legibility bias=%.2f; seed=%lu (next sentence).\r\n",double(bias_),(unsigned long)seed_);
        return true;
    }
    void command_error() {
        ++stats_->rejected;
        printf("Invalid setting. Use :bias 0..2 or :seed 1..4294967295.\r\n");
    }
    StreamStats* stats_; char line_[76]{}; unsigned length_=0,excess_=0,invalid_=0;
    float bias_=1.0f;uint32_t seed_=42;
    bool cr_=false; std::atomic<uint32_t> generation_{0}; SentenceSnapshot accepted_{};
};
