// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "../../../common/nodes_impl/c/NodeSupport.hpp"
#include "vad_fade.h"
#include "vad_ui.h"
// Event thread owns fade and backbuffer state; no inference or audio waits here.
class VADLCD : public StreamNode {
public:
 enum selector {selSnapshot=0};static std::array<uint16_t,1> selectors;
 explicit VADLCD(const VADLCDParams& p):p_(p) {}
 cg_status init() final {
  return p_.hw_.stats && p_.hw_.framebuffers[0] && p_.hw_.framebuffers[1] && p_.hw_.lcd_present && p_.fade_ms>0 && p_.fps>0 ? CG_SUCCESS:CG_INIT_FAILURE;
 }
 cg_status processEvent(int port,Event&& event) final {
  if(port!=0 || event.event_id!=selectors[selSnapshot] || !event.wellFormed<uint32_t,uint32_t>())return CG_BUFFER_ERROR;
  uint32_t start=osKernelGetSysTimerCount();auto& s=*p_.hw_.stats;++s.lcd_events_received;
  event.apply<uint32_t,uint32_t>([this,&s](uint32_t sequence,uint32_t tick) {
   if(sequence!=sequence_) {sequence_=sequence;detected_=tick;active_=true;++s.fade_restarts;}
  });
  uint32_t duration=uint64_t(osKernelGetTickFreq())*p_.fade_ms/1000U;
  unsigned alpha=vad_fade_alpha(osKernelGetTickCount(),detected_,duration,active_);
  if(active_ && !alpha){active_=false;++s.fade_expiries;}
  vad_render(p_.hw_.framebuffers[back_],alpha);
  int error=p_.hw_.lcd_present(p_.hw_.framebuffers[back_]);if(error)return static_cast<cg_status>(error);
  s.display_alpha=alpha;++s.frames_presented;back_^=1U;
  if(osKernelGetSysTimerCount()-start>osKernelGetSysTimerFreq()/p_.fps)++s.lcd_deadline_misses;
  return CG_SUCCESS;
 }
private:
 VADLCDParams p_;uint32_t sequence_=0,detected_=0;bool active_=false;unsigned back_=1;
};
