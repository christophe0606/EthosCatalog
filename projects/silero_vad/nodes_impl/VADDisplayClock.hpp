// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "../../../common/nodes_impl/c/NodeSupport.hpp"
#include "EventQueue.hpp"
// At most one small snapshot per display period. Retain newest detection on a
// failed send so transient queue pressure never loses the fade reset forever.
template<typename IN,int inputSamples>
class VADDisplayClock : public GenericSink<IN,inputSamples> {
public:
 enum selector {selSnapshot=0}; static std::array<uint16_t,1> selectors;
 VADDisplayClock(FIFOBase<IN>& in,EventQueue* queue,const VADDisplayClockParams& p)
 :GenericSink<IN,inputSamples>(in),events_(queue),p_(p) {static_assert(inputSamples==1);}
 cg_status init() final {return p_.hw_.stats && p_.fps>0 ? CG_SUCCESS:CG_INIT_FAILURE;}
 void subscribe(int port,StreamNode& dst,int dstPort) final {if(port==0)events_.subscribe(dst,dstPort);}
 int run() final {
  float probability=*this->getReadBuffer();auto& s=*p_.hw_.stats;uint32_t now=osKernelGetTickCount();
  if(probability>=p_.threshold) {last_=now;++sequence_;++s.detections;s.last_detection_tick=now;}
  if(!sent_ || int32_t(now-next_)>=0) {
   if(events_.sendAsync(kNormalPriority,selectors[selSnapshot],sequence_,last_))++s.lcd_events_sent;
   else ++s.lcd_events_dropped;
   uint32_t period=std::max(1U,osKernelGetTickFreq()/unsigned(p_.fps));
   next_=sent_?next_+period:now+period;
   if(int32_t(now-next_)>=0)next_=now+period;
   sent_=true;
  }
  uint32_t elapsed=osKernelGetSysTimerCount()-s.audio_started;
  if(elapsed>s.audio_ticks_max)s.audio_ticks_max=elapsed;
  if(elapsed>uint64_t(osKernelGetSysTimerFreq())*MIC_BLOCK_SAMPLES/MIC_SAMPLE_RATE)++s.audio_deadline_misses;
  return CG_SUCCESS;
 }
private:
 EventOutput events_;VADDisplayClockParams p_;uint32_t sequence_=0,last_=0,next_=0;bool sent_=false;
};
