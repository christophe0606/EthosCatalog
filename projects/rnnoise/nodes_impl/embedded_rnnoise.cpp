// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "embedded_rnnoise.h"
#include "embedded_runtime.hpp"
#include "cmsis_os2.h"
#include <array>
#include <cstring>
extern "C" { volatile int32_t rnnoise_runtime_error,rnnoise_load_error; }
void rnnoise_reset(RNNoiseState &s) { std::memset(s.vad,-1,24);std::memset(s.noise,-128,48);std::memset(s.denoise,-1,96); }
int rnnoise_forward(void *opaque,int8_t *features,RNNoiseState &s,int8_t *outputs,uint32_t *ticks)
{
    using namespace executorch::aten;
    if(!opaque||!features||!outputs)return -400;
    auto *module=static_cast<arm::embedded::EmbeddedModule*>(opaque);
    auto loaded=module->load_method("forward");
    rnnoise_load_error=static_cast<int32_t>(loaded);
    if(loaded!=executorch::runtime::Error::Ok)return -403;
    std::array<SizesType,3> fs{1,1,42};std::array<DimOrderType,3> fo{0,1,2};
    std::array<SizesType,2> vs{1,24},ns{1,48},ds{1,96};std::array<DimOrderType,2> so{0,1};
    TensorImpl a(ScalarType::Char,3,fs.data(),features,fo.data());
    TensorImpl b(ScalarType::Char,2,vs.data(),s.vad,so.data());
    TensorImpl c(ScalarType::Char,2,ns.data(),s.noise,so.data());
    TensorImpl d(ScalarType::Char,2,ds.data(),s.denoise,so.data());
    std::vector<executorch::runtime::EValue> input{Tensor(&a),Tensor(&b),Tensor(&c),Tensor(&d)};
    uint32_t start=osKernelGetSysTimerCount();auto result=module->forward(input);
    if(ticks)*ticks=osKernelGetSysTimerCount()-start;
    if(!result.ok()) { rnnoise_runtime_error=static_cast<int32_t>(result.error());return -401; }
    if(result->size()!=5)return -401;
    const size_t counts[]{96,22,48,24,1};size_t offset=0;
    for(unsigned i=0;i<5;++i) {
        if(!(*result)[i].isTensor())return -402;
        Tensor out=(*result)[i].toTensor();
        if(out.scalar_type()!=ScalarType::Char||out.numel()!=counts[i])return -402;
        std::memcpy(outputs+offset,out.const_data_ptr<int8_t>(),counts[i]);offset+=counts[i];
    }
    std::memcpy(s.denoise,outputs,96);std::memcpy(s.noise,outputs+118,48);std::memcpy(s.vad,outputs+166,24);
    return 0;
}
