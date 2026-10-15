// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "NodeSupport.hpp"
template<typename IN, int inputSamples, typename OUT, int outputSamples>
class Magnitude : public GenericNode<IN,inputSamples,OUT,outputSamples> {
public:
    Magnitude(FIFOBase<IN>& in, FIFOBase<OUT>& out) : GenericNode<IN,inputSamples,OUT,outputSamples>(in,out) {
        static_assert(inputSamples == 1 && outputSamples == 1 && block_samples<OUT> == block_samples<IN>/2+1);
    }
    int run() final {
        auto* in = this->getReadBuffer(); auto* out = this->getWriteBuffer();
        out->info = in->info;
        if (out->info.valid) fft_magnitude(in->samples, out->samples, block_samples<IN>, in->info.amplitude_scale);
        return CG_SUCCESS;
    }
};
