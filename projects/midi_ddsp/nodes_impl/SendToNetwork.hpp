// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "../../../common/nodes_impl/c/NodeSupport.hpp"
#include "EventQueue.hpp"
#include "app_params.h"
#include <atomic>
extern "C" {extern volatile uint32_t midi_event_dropped,midi_network_requests,midi_network_completions;}
// One outstanding request. Only its matching completion acknowledgment can
// release flow control; CPU preparation is visibly separate from PTE nodes.
template<typename IN,int n,typename OUT,int no>class SendToNetwork:public GenericNode<IN,n,OUT,no>{
 HardwareParams hw_;EventOutput requests_[8];std::atomic<bool> waiting_{false},deferred_{false};int next_=-1;unsigned method_=0;
 cg_status send(int method){if(method<-1)return CG_INIT_FAILURE;if(method==-1)return CG_SUCCESS;if(waiting_||method>7)return CG_BUFFER_ERROR;
  waiting_=true;method_=method;++midi_network_requests;if(requests_[method].sendAsync(kNormalPriority,selectors[0],uint32_t(method)))return CG_SUCCESS;
  ++midi_event_dropped;return CG_BUFFER_ERROR;}
public:
 static std::array<uint16_t,3>selectors;
 SendToNetwork(FIFOBase<IN>&i,FIFOBase<OUT>&o,EventQueue*q,const SendToNetworkParams&p):GenericNode<IN,n,OUT,no>(i,o),hw_(p.hw_),requests_{EventOutput(q),EventOutput(q),EventOutput(q),EventOutput(q),EventOutput(q),EventOutput(q),EventOutput(q),EventOutput(q)}{}
 void subscribe(int port,StreamNode&dst,int dp)final{requests_[port].subscribe(dst,dp);}
 int run()final{*this->getWriteBuffer()=0;if(*this->getReadBuffer()){if(waiting_){deferred_=true;return CG_SUCCESS;}return send(hw_.prepare(hw_.network,-1,nullptr));}return CG_SUCCESS;}
 cg_status processEvent(int port,Event&&e)final{
  if(!waiting_||unsigned(port/2)!=method_)return CG_BUFFER_ERROR;
  if(port%2==0&&e.event_id==selectors[1]&&e.wellFormed<uint64_t>()){
   e.apply<uint64_t>([&](uint64_t ptr){next_=hw_.prepare(hw_.network,int(method_),reinterpret_cast<const float*>(uintptr_t(ptr)));});return next_<-1?CG_INIT_FAILURE:CG_SUCCESS;}
  if(port%2==1&&e.event_id==selectors[2]&&e.wellFormed<uint32_t>()){
   ++midi_network_completions;waiting_=false;if(next_<0&&deferred_){deferred_=false;next_=hw_.prepare(hw_.network,-1,nullptr);}return send(next_);}
  return CG_BUFFER_ERROR;
 }
};
