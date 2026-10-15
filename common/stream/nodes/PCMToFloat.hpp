// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "NodeSupport.hpp"
template<typename IN, int inputSamples, typename OUT, int outputSamples>
class PCMToFloat : public GenericNode<IN, inputSamples, OUT, outputSamples> {
public:
    PCMToFloat(FIFOBase<IN>& in, FIFOBase<OUT>& out) : GenericNode<IN,inputSamples,OUT,outputSamples>(in,out) {
        static_assert(inputSamples == 1 && outputSamples == 1 && block_samples<IN> == block_samples<OUT>);
    }
    int run() final {
        auto* in = this->getReadBuffer(); auto* out = this->getWriteBuffer();
        out->info = in->info;
        pcm_to_float(in->samples, out->samples, block_samples<IN>);
        return CG_SUCCESS;
    }
};
