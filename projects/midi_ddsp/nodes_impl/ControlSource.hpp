// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "../../../common/nodes_impl/c/NodeSupport.hpp"
#include "app_params.h"
template<typename IN,int ni,typename OUT,int n>class ControlSource:public GenericNode<IN,ni,OUT,n>{
 HardwareParams hw_;
public:ControlSource(FIFOBase<IN>&i,FIFOBase<OUT>&o,const ControlSourceParams&p):GenericNode<IN,ni,OUT,n>(i,o),hw_(p.hw_){}
 int run()final{this->getReadBuffer();return hw_.controls(hw_.network,this->getWriteBuffer());}
};
