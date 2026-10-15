// SPDX-License-Identifier: Apache-2.0
#include "nodes.h"
#include <math.h>
void pcm_to_float(const int16_t *pcm, float *out, size_t count)
{
    for (size_t i = 0; i < count; ++i) out[i] = pcm[i] / 32768.0f;
}
void remove_dc(const float *in, float *out, size_t count)
{
    float mean = 0;
    if (!count) return;
    for (size_t i = 0; i < count; ++i) mean += in[i];
    mean /= (float)count;
    for (size_t i = 0; i < count; ++i) out[i] = in[i] - mean;
}
float hann_window_init(float *window, size_t count)
{
    float sum = 0;
    for (size_t i = 0; i < count; ++i) {
        window[i] = 0.5f - 0.5f * cosf(6.283185307179586f * i / count);
        sum += window[i];
    }
    return sum > 0 ? 2.0f / sum : 0;
}
void apply_window(const float *in, const float *window, float *out, size_t count)
{
    for (size_t i = 0; i < count; ++i) out[i] = in[i] * window[i];
}
