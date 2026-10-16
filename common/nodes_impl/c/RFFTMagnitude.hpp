// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif
// Requires CMSIS-DSP fast F32 RFFT packing, not interleaved CFFT output.
// Read fft_size packed floats (DC at [0], Nyquist at [1]); write
// fft_size/2+1 real magnitudes. Only the interior bins are complex pairs.
void fft_magnitude(const float *packed, float *out, size_t fft_size, float scale);

#ifdef __cplusplus
}
#include "NodeSupport.hpp"
template<typename IN, int inputSamples, typename OUT, int outputSamples>
class RFFTMagnitude : public GenericNode<IN,inputSamples,OUT,outputSamples> {
public:
    RFFTMagnitude(FIFOBase<IN>& in, FIFOBase<OUT>& out, const RFFTMagnitudeParams& params)
        : GenericNode<IN,inputSamples,OUT,outputSamples>(in,out), scale_(params.amplitude_scale) {
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
#endif // __cplusplus
