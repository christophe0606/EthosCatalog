// SPDX-License-Identifier: Apache-2.0
#include "embedded_kws.h"
#include "embedded_runtime.hpp"
#include "cmsis_os2.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>
namespace { alignas(32) int8_t input[490] __attribute__((section(APP_POOL_SECTION))); }
int kws_forward_int8(void *module,const int8_t *features,int8_t *scores,uint32_t *ticks) {
    using namespace executorch::aten;
    std::memcpy(input,features,sizeof(input));
    std::array<SizesType,4> sizes{1,49,10,1};
    std::array<DimOrderType,4> order{0,1,2,3};
    TensorImpl impl(ScalarType::Char,sizes.size(),sizes.data(),input,order.data());
    Tensor tensor(&impl);
    uint32_t start=osKernelGetSysTimerCount();
    auto result=static_cast<arm::embedded::EmbeddedModule*>(module)->forward(tensor);
    *ticks=osKernelGetSysTimerCount()-start;
    if (!result.ok()) return -500-int(result.error());
    if (result->size()!=1 || !result->front().isTensor()) return -601;
    Tensor out=result->front().toTensor();
    if (out.scalar_type()!=ScalarType::Char || out.dim()!=2 || out.size(0)!=1 || out.size(1)!=12) return -602;
    std::memcpy(scores,out.const_data_ptr<int8_t>(),12);
    return 0;
}
int kws_forward(void *module,const float *features,int8_t *scores,uint32_t *ticks) {
    int8_t quantized[490];
    for (unsigned i=0;i<490;++i) {
        float q=std::round(features[i]/0.20109497010707855f-5.0f);
        quantized[i]=static_cast<int8_t>(std::max(-128.0f,std::min(127.0f,q)));
    }
    return kws_forward_int8(module,quantized,scores,ticks);
}
unsigned kws_classify(const int8_t *scores,float *probability) {
    unsigned best=0;
    for(unsigned i=1;i<12;++i) if(scores[i]>scores[best]) best=i;
    float sum=0;
    for(unsigned i=0;i<12;++i) sum+=std::exp((int(scores[i])-int(scores[best]))*0.05110505223274231f);
    *probability=1.0f/sum;
    return best;
}
