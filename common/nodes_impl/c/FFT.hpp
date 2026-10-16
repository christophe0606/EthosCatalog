// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stddef.h>
#include "arm_math.h"

#ifdef __cplusplus
extern "C" {
#endif
int fft_2048_init(arm_rfft_fast_instance_f32 *fft);
// N real samples -> N packed floats: Re(X[0]), Re(X[N/2]),
// Re(X[1]), Im(X[1]), ..., Re(X[N/2-1]), Im(X[N/2-1]).
void fft_forward(arm_rfft_fast_instance_f32 *fft, float *in, float *packed);

#ifdef __cplusplus
}
#include "NodeSupport.hpp"
template<typename IN, int inputSamples, typename OUT, int outputSamples>
class FFT : public GenericNode<IN,inputSamples,OUT,outputSamples> {
public:
    FFT(FIFOBase<IN>& in, FIFOBase<OUT>& out) : GenericNode<IN,inputSamples,OUT,outputSamples>(in,out) {
        static_assert(std::is_same_v<IN,float> && std::is_same_v<OUT,float>);
        static_assert(inputSamples >= 32 && inputSamples <= 4096 && (inputSamples & (inputSamples-1)) == 0);
        static_assert(outputSamples == inputSamples);
    }
    cg_status init() final {
        // Only the selected initializer and its tables are referenced.
        arm_status status;
        if constexpr (inputSamples == 32) status = arm_rfft_fast_init_32_f32(&fft_);
        else if constexpr (inputSamples == 64) status = arm_rfft_fast_init_64_f32(&fft_);
        else if constexpr (inputSamples == 128) status = arm_rfft_fast_init_128_f32(&fft_);
        else if constexpr (inputSamples == 256) status = arm_rfft_fast_init_256_f32(&fft_);
        else if constexpr (inputSamples == 512) status = arm_rfft_fast_init_512_f32(&fft_);
        else if constexpr (inputSamples == 1024) status = arm_rfft_fast_init_1024_f32(&fft_);
        else if constexpr (inputSamples == 2048) status = arm_rfft_fast_init_2048_f32(&fft_);
        else if constexpr (inputSamples == 4096) status = arm_rfft_fast_init_4096_f32(&fft_);
        return status == ARM_MATH_SUCCESS ? CG_SUCCESS : CG_INIT_FAILURE;
    }
    int run() final {
        auto* in = this->getReadBuffer(); auto* out = this->getWriteBuffer();
        fft_forward(&fft_, in, out);
        return CG_SUCCESS;
    }
private:
    arm_rfft_fast_instance_f32 fft_{};
};
#endif // __cplusplus
