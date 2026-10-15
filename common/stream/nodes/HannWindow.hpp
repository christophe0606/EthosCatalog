// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "NodeSupport.hpp"
template<typename IN, int inputSamples, typename OUT, int outputSamples>
class HannWindow : public GenericNode<IN,inputSamples,OUT,outputSamples> {
public:
    HannWindow(FIFOBase<IN>& in, FIFOBase<OUT>& out) : GenericNode<IN,inputSamples,OUT,outputSamples>(in,out) {
        static_assert(inputSamples == 1 && outputSamples == 1 && block_samples<IN> == block_samples<OUT>);
    }
    cg_status init() final { scale_ = hann_window_init(window_, block_samples<IN>); return CG_SUCCESS; }
    int run() final {
        auto* in = this->getReadBuffer(); auto* out = this->getWriteBuffer();
        out->info = in->info; out->info.amplitude_scale = scale_;
        if (out->info.valid) apply_window(in->samples, window_, out->samples, block_samples<IN>);
        return CG_SUCCESS;
    }
private:
    float window_[block_samples<IN>];
    float scale_ = 0;
};
