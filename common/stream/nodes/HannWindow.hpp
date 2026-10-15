// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "NodeSupport.hpp"
template<typename IN, int inputSamples, typename OUT, int outputSamples>
class HannWindow : public GenericNode<IN,inputSamples,OUT,outputSamples> {
public:
    HannWindow(FIFOBase<IN>& in, FIFOBase<OUT>& out) : GenericNode<IN,inputSamples,OUT,outputSamples>(in,out) {
        static_assert(std::is_same_v<IN,float> && std::is_same_v<OUT,float>);
        static_assert(inputSamples > 0 && inputSamples == outputSamples);
    }
    cg_status init() final { hann_window_init(window_, inputSamples); return CG_SUCCESS; }
    int run() final {
        auto* in = this->getReadBuffer(); auto* out = this->getWriteBuffer();
        apply_window(in, window_, out, inputSamples);
        return CG_SUCCESS;
    }
private:
    float window_[inputSamples];
};
