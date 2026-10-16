// SPDX-License-Identifier: Apache-2.0
#include "PCMToFloat.hpp"
#include "RemoveDC.hpp"
#include "HannWindow.hpp"
#include "dsp/basic_math_functions.h"
#include "dsp/statistics_functions.h"
#include "dsp/support_functions.h"
#include "dsp/window_functions.h"

void pcm_to_float(const int16_t *pcm, float *out, size_t count)
{
    if (!count) return;
    /* Every Q15 value is exactly representable in float; no clipping is needed. */
    arm_q15_to_float(pcm, out, (uint32_t)count);
}

void remove_dc(const float *in, float *out, size_t count)
{
    float mean;
    if (!count) return;
    arm_mean_f32(in, (uint32_t)count, &mean);
    arm_offset_f32(in, -mean, out, (uint32_t)count);
}

float hann_window_init(float *window, size_t count)
{
    float mean;
    if (!count) return 0;
    arm_hanning_f32(window, (uint32_t)count);
    arm_mean_f32(window, (uint32_t)count, &mean);
    /* One-sided amplitude normalization, including the window's coherent gain. */
    const float sum = mean * (float)count;
    return sum > 0 ? 2.0f / sum : 0;
}

void apply_window(const float *in, const float *window, float *out, size_t count)
{
    if (!count) return;
    arm_mult_f32(in, window, out, (uint32_t)count);
}
