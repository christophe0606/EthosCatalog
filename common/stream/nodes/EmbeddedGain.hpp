// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "NodeSupport.hpp"
#include "embedded_gain.h"
template<typename IN, int inputSamples, typename OUT, int outputSamples>
class EmbeddedGain : public GenericNode<IN,inputSamples,OUT,outputSamples> {
public:
    EmbeddedGain(FIFOBase<IN>& in, FIFOBase<OUT>& out, const EmbeddedGainParams& params)
        : GenericNode<IN,inputSamples,OUT,outputSamples>(in,out), hw_(params.hw_) {
        static_assert(inputSamples == 1 && outputSamples == 1 && block_samples<IN> == block_samples<OUT>);
    }
    cg_status init() final { return hw_.module && hw_.inference_input ? CG_SUCCESS : CG_INIT_FAILURE; }
    int run() final {
        auto* in = this->getReadBuffer(); auto* out = this->getWriteBuffer();
        out->info = in->info;
        // Inference uses NPU-visible SRAM, independent of FIFO placement.
        std::memcpy(hw_.inference_input, in->samples, sizeof(in->samples));
        return embedded_gain_forward(hw_.module, hw_.inference_input, out->samples, block_samples<IN>);
    }
private:
    HardwareParams hw_;
};
