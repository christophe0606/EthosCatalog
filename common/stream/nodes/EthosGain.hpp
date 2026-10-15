// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "NodeSupport.hpp"
#include "embedded_gain.h"

template<typename IN, int inputSamples, typename OUT, int outputSamples>
class EthosGain : public GenericNode<IN,inputSamples,OUT,outputSamples> {
public:
    EthosGain(FIFOBase<IN>& in, FIFOBase<OUT>& out, const EthosGainParams& params)
        : GenericNode<IN,inputSamples,OUT,outputSamples>(in,out), hw_(params.hw_) {
        static_assert(std::is_same_v<IN,float> && std::is_same_v<OUT,float>);
        static_assert(inputSamples > 0 && inputSamples == outputSamples);
    }
    cg_status init() final {
        return hw_.module && hw_.inference_input && hw_.inference_samples == inputSamples
            ? CG_SUCCESS : CG_INIT_FAILURE;
    }
    int run() final {
        auto* in = this->getReadBuffer(); auto* out = this->getWriteBuffer();
        // Inference uses NPU-visible SRAM, independent of FIFO placement.
        std::memcpy(hw_.inference_input, in, inputSamples*sizeof(IN));
        return embedded_gain_forward(hw_.module, hw_.inference_input, out, inputSamples);
    }
private:
    HardwareParams hw_;
};
