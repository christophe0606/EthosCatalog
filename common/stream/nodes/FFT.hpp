// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "NodeSupport.hpp"
template<typename IN, int inputSamples, typename OUT, int outputSamples>
class FFT : public GenericNode<IN,inputSamples,OUT,outputSamples> {
public:
    FFT(FIFOBase<IN>& in, FIFOBase<OUT>& out) : GenericNode<IN,inputSamples,OUT,outputSamples>(in,out) {
        static_assert(inputSamples == 1 && outputSamples == 1 && block_samples<IN> == 2048 && block_samples<OUT> == 2048);
    }
    cg_status init() final { return fft_2048_init(&fft_) ? CG_INIT_FAILURE : CG_SUCCESS; }
    int run() final {
        auto* in = this->getReadBuffer(); auto* out = this->getWriteBuffer();
        out->info = in->info;
        if (out->info.valid) fft_forward(&fft_, in->samples, out->samples);
        return CG_SUCCESS;
    }
private:
    arm_rfft_fast_instance_f32 fft_{};
};
