// SPDX-License-Identifier: Apache-2.0
#include "FFT.hpp"
#include "RFFTMagnitude.hpp"
#include <math.h>
// CMSIS-DSP selects its portable or accelerated implementation for the target.
int fft_2048_init(arm_rfft_fast_instance_f32 *fft)
{
    return arm_rfft_fast_init_2048_f32(fft) == ARM_MATH_SUCCESS ? 0 : -1;
}
void fft_forward(arm_rfft_fast_instance_f32 *fft, float *in, float *packed)
{
    arm_rfft_fast_f32(fft, in, packed, 0);
}
void fft_magnitude(const float *packed, float *out, size_t size, float scale)
{
    // CMSIS fast RFFT packs the real DC and Nyquist bins together in the
    // first pair. They are separate bins, so do not take a complex magnitude
    // of that pair. One-sided amplitude scaling doubles only interior bins.
    out[0] = fabsf(packed[0]) * scale * 0.5f;
    out[size / 2] = fabsf(packed[1]) * scale * 0.5f;
    arm_cmplx_mag_f32(packed + 2, out + 1, size / 2 - 1);
    arm_scale_f32(out + 1, scale, out + 1, size / 2 - 1);
}
