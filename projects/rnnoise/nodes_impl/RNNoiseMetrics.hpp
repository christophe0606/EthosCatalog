// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "../../../common/nodes_impl/c/NodeSupport.hpp"
#include "EventQueue.hpp"
#include <cmath>
// Consume each enhanced frame immediately: no utterance accumulation or playback.
template<typename IN,int n>class RNNoiseMetrics:public GenericSink<IN,n>{
 EventOutput events_;RNNoiseMetricsParams p_;uint32_t last_=0;bool sent_=false;
public:
 enum selector{selSnapshot=0};static std::array<uint16_t,1> selectors;
 RNNoiseMetrics(FIFOBase<IN>& i,EventQueue* q,const RNNoiseMetricsParams& p):GenericSink<IN,n>(i),events_(q),p_(p){static_assert(n==483);}
 cg_status init() final{return p_.hw_.stats?CG_SUCCESS:CG_INIT_FAILURE;}
 void subscribe(int port,StreamNode& dst,int dstPort) final{if(!port)events_.subscribe(dst,dstPort);}
 int run() final{auto* x=this->getReadBuffer();auto& s=*p_.hw_.stats;float energy=0;for(unsigned i=0;i<480;++i)energy+=x[i]*x[i];s.output_rms=std::sqrt(energy/480);s.null_samples+=480;s.input_dbfs=20*std::log10(std::max(1.e-6f,x[480]));s.mean_gain=x[482];s.suppression_db=-20*std::log10(std::max(1.e-6f,x[482]));
 uint32_t ticks=osKernelGetSysTimerCount()-s.audio_started;if(ticks>s.audio_ticks_max)s.audio_ticks_max=ticks;if(ticks>osKernelGetSysTimerFreq()/100)++s.audio_deadline_misses;
 uint32_t now=osKernelGetTickCount();if(!sent_||now-last_>=osKernelGetTickFreq()/LCD_FPS){float ms=1000.f*s.inference_ticks_max/osKernelGetSysTimerFreq();float frame=1000.f*ticks/osKernelGetSysTimerFreq();if(events_.sendAsync(kNormalPriority,selectors[selSnapshot],x[481],float(s.input_dbfs),float(s.suppression_db),ms,frame,uint32_t(s.inferences),uint32_t(s.audio_discontinuities)))++s.lcd_events_sent;else ++s.lcd_events_dropped;last_=now;sent_=true;}return CG_SUCCESS;}
};
