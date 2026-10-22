// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "handwriting_engine.hpp"
#include "embedded_runtime.hpp"
#include "cmsis_os2.h"
#include <array>
#include <cstring>

namespace handwriting {
int Engine::dense(unsigned method,const float *input,float *output,uint32_t *ticks) {
    using namespace executorch::aten;
    if(method>=MethodCount || !module_ || !input || !output) return -420;
    // Boundary quantization produces the NPU input in the planned pool.
    std::array<SizesType,2> sizes{1,static_cast<SizesType>(input_sizes[method])};
    std::array<DimOrderType,2> order{0,1};
    TensorImpl impl(ScalarType::Float,2,sizes.data(),const_cast<float*>(input),order.data());
    const uint32_t start=osKernelGetSysTimerCount();
    auto result=module_->execute(method_names[method],Tensor(&impl));
    if(!result.ok() || result->size()!=1 || !result->front().isTensor()) return -421;
    Tensor out=result->front().toTensor();
    if(out.scalar_type()!=ScalarType::Float || out.numel()!=output_sizes[method]) return -422;
    std::memcpy(output,out.const_data_ptr<float>(),output_sizes[method]*sizeof(float));
    if(ticks) *ticks=osKernelGetSysTimerCount()-start;
    for(unsigned i=0;i<output_sizes[method];++i) {
        uint32_t bits; std::memcpy(&bits,output+i,sizeof(bits));
        if((bits&0x7f800000U)==0x7f800000U) return -423;
    }
    return 0;
}
}
