// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "NodeSupport.hpp"
template<typename IN, int inputSamples, typename OUT, int outputSamples>
class Magnitude : public GenericNode<IN,inputSamples,OUT,outputSamples> {
public:
    Magnitude(FIFOBase<IN>& in, FIFOBase<OUT>& out, const MagnitudeParams& params) : GenericNode<IN,inputSamples,OUT,outputSamples>(in,out), scale_(params.amplitude_scale) {
        static_assert(std::is_same_v<IN,float> && std::is_same_v<OUT,float>);
        static_assert(inputSamples >= 4 && inputSamples % 2 == 0 && outputSamples == inputSamples/2+1);
    }
    int run() final {
        auto* in = this->getReadBuffer(); auto* out = this->getWriteBuffer();
        fft_magnitude(in, out, inputSamples, scale_);
        return CG_SUCCESS;
    }
private:
    float scale_;
};
