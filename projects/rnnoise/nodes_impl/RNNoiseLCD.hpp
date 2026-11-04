// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "../../../common/nodes_impl/c/NodeSupport.hpp"
extern "C" void rnnoise_render(uint16_t*,float,float,float,float,float,uint32_t,uint32_t);
class RNNoiseLCD:public StreamNode{
 RNNoiseLCDParams p_;unsigned back_=1;
public:
 enum selector{selSnapshot=0};static std::array<uint16_t,1> selectors;
 RNNoiseLCD(const RNNoiseLCDParams& p):p_(p){}
 cg_status init() final{return p_.hw_.stats&&p_.hw_.framebuffers[0]&&p_.hw_.framebuffers[1]&&p_.hw_.lcd_present?CG_SUCCESS:CG_INIT_FAILURE;}
 cg_status processEvent(int port,Event&& e) final{if(port||e.event_id!=selectors[selSnapshot]||!e.wellFormed<float,float,float,float,float,uint32_t,uint32_t>())return CG_BUFFER_ERROR;uint32_t t=osKernelGetSysTimerCount();e.apply<float,float,float,float,float,uint32_t,uint32_t>([this](float p,float db,float gain,float ms,float frame,uint32_t count,uint32_t gaps){rnnoise_render(p_.hw_.framebuffers[back_],p,db,gain,ms,frame,count,gaps);});int error=p_.hw_.lcd_present(p_.hw_.framebuffers[back_]);if(error)return cg_status(error);back_^=1;++p_.hw_.stats->frames_presented;++p_.hw_.stats->lcd_events_received;if(osKernelGetSysTimerCount()-t>osKernelGetSysTimerFreq()/LCD_FPS)++p_.hw_.stats->lcd_deadline_misses;return CG_SUCCESS;}
};
