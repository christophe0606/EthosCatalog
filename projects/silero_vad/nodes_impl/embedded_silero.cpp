// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "embedded_silero.h"
#include "embedded_runtime.hpp"
#include "cmsis_os2.h"
#include <array>
#include <cstring>
int silero_forward(void *opaque,float *audio,float *h,float *c,
                   float *probability,float *next_h,float *next_c,uint32_t *ticks)
{
    using namespace executorch::aten;
    if (!opaque||!audio||!h||!c||!probability||!next_h||!next_c) return -400;
    auto *module=static_cast<arm::embedded::EmbeddedModule*>(opaque);
    std::array<SizesType,2> audio_shape{1,576},state_shape{1,128};
    std::array<DimOrderType,2> order{0,1};
    TensorImpl a(ScalarType::Float,2,audio_shape.data(),audio,order.data());
    TensorImpl b(ScalarType::Float,2,state_shape.data(),h,order.data());
    TensorImpl d(ScalarType::Float,2,state_shape.data(),c,order.data());
    std::vector<executorch::runtime::EValue> inputs{Tensor(&a),Tensor(&b),Tensor(&d)};
    uint32_t start=osKernelGetSysTimerCount();
    auto result=module->forward(inputs);
    if (ticks) *ticks=osKernelGetSysTimerCount()-start;
    if (!result.ok()||result->size()!=3) return -401;
    float *outputs[]={probability,next_h,next_c};
    const size_t counts[]={1,128,128};
    for (unsigned i=0;i<3;++i) {
        if (!(*result)[i].isTensor()) return -402;
        Tensor out=(*result)[i].toTensor();
        if(out.scalar_type()!=ScalarType::Float||out.numel()!=counts[i])return -402;
        std::memcpy(outputs[i],out.const_data_ptr<float>(),counts[i]*sizeof(float));
    }
    return 0;
}
