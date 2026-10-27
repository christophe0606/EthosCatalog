// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "Protocol.hpp"
template<typename IN,int ni,typename OUT,int no> class LineInput:public GenericNode<IN,ni,OUT,no> {
public:
    enum selector {selAdvance}; static std::array<uint16_t,1> selectors;
    LineInput(FIFOBase<IN>&i,FIFOBase<OUT>&o,const LineInputParams&p):GenericNode<IN,ni,OUT,no>(i,o),stats_(p.hw_.stats){}
    int run() final {
        const auto& b=*this->getReadBuffer();
        for(unsigned i=0;i<b.count;++i){char c=char(b.bytes[i]);
            // Keep escape state across UART blocks (CSI and application cursor
            // mode both send Up as a multi-byte sequence ending in 'A').
            if(c=='\x1b'){escape_=EscapeState::Start;cr_=false;continue;}
            if(escape_!=EscapeState::None){
                if(c=='\r'||c=='\n')escape_=EscapeState::None;
                else if(escape_==EscapeState::Start){
                    escape_=(c=='['||c=='O')?EscapeState::Sequence:EscapeState::None;
                    continue;
                }else{
                    if(c>=0x40&&c<=0x7e){escape_=EscapeState::None;if(c=='A')recall();}
                    else if(c<0x20||c>0x3f)escape_=EscapeState::None;
                    continue;
                }
            }
            if(c=='\r'||c=='\n') {if(c=='\n'&&cr_){cr_=false;continue;}cr_=c=='\r';
                std::fputs("\r\n",stdout);
                if(overflow_){++stats_->rejected;printf("REJECT line exceeds160 ASCII characters\n");}
                else if(length_){editing_[length_]=0;std::memcpy(line_.text,editing_,length_+1);line_.generation=++generation_;}
                length_=0;overflow_=false;
            } else if(c==8||c==127){if(length_){--length_;std::fputs("\b \b",stdout);}}
            else if(c>=32&&c<=126){cr_=false;if(length_<160){editing_[length_++]=c;std::putchar(c);}else {if(!overflow_)std::putchar('\a');overflow_=true;}}
            else if(c!='\t'){overflow_=true;}
        }
        stats_->input_editing=length_!=0||overflow_||escape_!=EscapeState::None;
        if(b.count)std::fflush(stdout);
        *this->getWriteBuffer()=line_;return CG_SUCCESS;
    }
private:
    void recall(){
        // line_ retains the last nonempty, non-overflowed submission. Recalling
        // only edits the input; Enter increments generation and submits it.
        if(!line_.text[0])return;
        for(unsigned left=length_;left;--left)std::fputs("\b \b",stdout);
        length_=std::strlen(line_.text);
        std::memcpy(editing_,line_.text,length_+1);overflow_=false;
        std::fputs(editing_,stdout);
    }
    enum class EscapeState {None,Start,Sequence};
    EscapeState escape_=EscapeState::None;
    StreamStats*stats_;TextLine line_{};char editing_[164]{};unsigned length_=0;uint32_t generation_=0;bool overflow_=false,cr_=false;
};
