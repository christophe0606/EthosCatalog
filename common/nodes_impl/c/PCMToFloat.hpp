// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
void pcm_to_float(const int16_t *pcm, float *out, size_t count);

#ifdef __cplusplus
}
#include "NodeSupport.hpp"
template<typename IN, int inputSamples, typename OUT, int outputSamples>
class PCMToFloat : public GenericNode<IN, inputSamples, OUT, outputSamples> {
public:
    PCMToFloat(FIFOBase<IN>& in, FIFOBase<OUT>& out) : GenericNode<IN,inputSamples,OUT,outputSamples>(in,out) {
        static_assert(std::is_same_v<IN,int16_t> && std::is_same_v<OUT,float>);
        static_assert(inputSamples > 0 && inputSamples == outputSamples);
    }
    int run() final {
        auto* in = this->getReadBuffer(); auto* out = this->getWriteBuffer();
        pcm_to_float(in, out, inputSamples);
        return CG_SUCCESS;
    }
};
#endif // __cplusplus
