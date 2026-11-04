// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "../../../common/nodes_impl/c/NodeSupport.hpp"
#include "EventQueue.hpp"
#include "app_params.h"
class MidiResultOutput:public EventOutput {
public:using EventOutput::EventOutput;
 template<typename T>bool deliver(uint32_t selector,T value){return sendCombinedValue(kNormalPriority,kSync,selector,0,value);}
};
// Exactly one ExecuTorch method. All score preparation, sampling and retained
// state updates are performed by the CPU schedule node handling its result.
class MidiMethod:public StreamNode {
public:
 enum selector {selExecute,selResult,selAck};static std::array<uint16_t,3>selectors;
 MidiMethod(EventQueue*q,const MidiMethodParams&p):result_(q),ack_(q),hw_(p.hw_),method_(p.method){}
 void subscribe(int port,StreamNode&dst,int dp) final {if(port==0)result_.subscribe(dst,dp);else if(port==1)ack_.subscribe(dst,dp);}
 cg_status processEvent(int port,Event&&e) final {
  if(port||e.event_id!=selectors[selExecute]||!e.wellFormed<uint32_t>())return CG_BUFFER_ERROR;
  const float *output=hw_.invoke(hw_.network,method_);if(!output)return CG_INIT_FAILURE;
  // Already on the network event thread. Synchronous result/ack delivery
  // avoids two redundant queue trips; the next request remains asynchronous.
  if(!result_.deliver(selectors[selResult],uint64_t(reinterpret_cast<uintptr_t>(output))))return CG_BUFFER_ERROR;
  return ack_.deliver(selectors[selAck],uint32_t(method_))?CG_SUCCESS:CG_BUFFER_ERROR;
 }
private:MidiResultOutput result_,ack_;HardwareParams hw_;unsigned method_;
};
