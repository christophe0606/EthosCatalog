// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "handwriting_engine.hpp"
#include "embedded_runtime.hpp"
#include "cmsis_os2.h"
#include <array>
#include <cstring>

namespace handwriting {
#if defined(HANDWRITING_TRANSFER_BENCHMARK)
extern bool benchmark_copy_output;
#endif
int Engine::dense_view(unsigned method,const float *input,const float *&output,uint32_t *ticks) {
    output=nullptr;
    using namespace executorch::aten;
    if(method>=MethodCount || !module_ || !input) return -420;
    // Boundary quantization produces the NPU input in the planned pool.
    std::array<SizesType,2> sizes{1,static_cast<SizesType>(input_sizes[method])};
    std::array<DimOrderType,2> order{0,1};
    TensorImpl impl(ScalarType::Float,2,sizes.data(),const_cast<float*>(input),order.data());
    const uint32_t start=osKernelGetSysTimerCount();
    auto result=module_->execute(method_names[method],Tensor(&impl));
    if(!result.ok() || result->size()!=1 || !result->front().isTensor()) return -421;
    Tensor out=result->front().toTensor();
    if(out.scalar_type()!=ScalarType::Float || out.numel()!=output_sizes[method]) return -422;
    const float *data=out.const_data_ptr<float>();
    if(!data) return -422;
#if defined(HANDWRITING_TRANSFER_BENCHMARK)
    // Same-binary A/B measurement. Normal firmware has no staging buffer.
    alignas(32) static float copied[1600];
    if(benchmark_copy_output) {
        std::memcpy(copied,data,output_sizes[method]*sizeof(float));
        data=copied;
    }
#endif
    for(unsigned i=0;i<output_sizes[method];++i) {
        uint32_t bits; std::memcpy(&bits,data+i,sizeof(bits));
        if((bits&0x7f800000U)==0x7f800000U) return -423;
    }
    // get_outputs() returns shallow tensor copies; destroying result leaves
    // the loaded method's storage alive. stage() consumes it before returning.
    output=data;
    if(ticks) *ticks=osKernelGetSysTimerCount()-start;
    return 0;
}
int Engine::dense(unsigned method,const float *input,float *output,uint32_t *ticks) {
    if(!output) return -420;
    const uint32_t start=osKernelGetSysTimerCount();
    const float *data=nullptr;
    int error=dense_view(method,input,data); if(error) return error;
    std::memcpy(output,data,output_sizes[method]*sizeof(float));
    if(ticks) *ticks=osKernelGetSysTimerCount()-start;
    return 0;
}
}
