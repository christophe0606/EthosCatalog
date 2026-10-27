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
            if(c=='\r'||c=='\n') {if(c=='\n'&&cr_){cr_=false;continue;}cr_=c=='\r';
                std::fputs("\r\n",stdout);
                if(overflow_){++stats_->rejected;printf("REJECT line exceeds160 ASCII characters\n");}
                else if(length_){editing_[length_]=0;std::memcpy(line_.text,editing_,length_+1);line_.generation=++generation_;}
                length_=0;overflow_=false;
            } else if(c==8||c==127){if(length_){--length_;std::fputs("\b \b",stdout);}}
            else if(c>=32&&c<=126){cr_=false;if(length_<160){editing_[length_++]=c;std::putchar(c);}else {if(!overflow_)std::putchar('\a');overflow_=true;}}
            else if(c!='\t'){overflow_=true;}
        }
        stats_->input_editing=length_!=0||overflow_;
        if(b.count)std::fflush(stdout);
        *this->getWriteBuffer()=line_;return CG_SUCCESS;
    }
private: StreamStats*stats_;TextLine line_{};char editing_[164]{};unsigned length_=0;uint32_t generation_=0;bool overflow_=false,cr_=false;
};
