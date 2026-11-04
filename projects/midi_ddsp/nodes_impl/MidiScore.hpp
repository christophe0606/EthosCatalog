// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "../../../common/nodes_impl/c/NodeSupport.hpp"
#include "app_params.h"
template<typename OUT,int n>class MidiScore:public GenericSource<OUT,n>{
 HardwareParams hw_;
public:MidiScore(FIFOBase<OUT>&o,const MidiScoreParams&p):GenericSource<OUT,n>(o),hw_(p.hw_){}
 int run()final{*this->getWriteBuffer()=hw_.next_window(hw_.network);return CG_SUCCESS;}
};
