// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif
void remove_dc(const float *in, float *out, size_t count);

#ifdef __cplusplus
}
#include "NodeSupport.hpp"
template<typename IN, int inputSamples, typename OUT, int outputSamples>
class RemoveDC : public GenericNode<IN,inputSamples,OUT,outputSamples> {
public:
    RemoveDC(FIFOBase<IN>& in, FIFOBase<OUT>& out) : GenericNode<IN,inputSamples,OUT,outputSamples>(in,out) {
        static_assert(std::is_same_v<IN,float> && std::is_same_v<OUT,float>);
        static_assert(inputSamples > 0 && inputSamples == outputSamples);
    }
    int run() final {
        auto* in = this->getReadBuffer(); auto* out = this->getWriteBuffer();
        remove_dc(in, out, inputSamples);
        return CG_SUCCESS;
    }
};
#endif // __cplusplus
