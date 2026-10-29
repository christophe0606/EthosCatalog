// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstdint>
#if defined(__ARM_FEATURE_MVE) && (__ARM_FEATURE_MVE & 2)
#include <arm_mve.h>
#define YAMNET_MVE_F32 1
#else
#define YAMNET_MVE_F32 0
#endif

namespace yamnet {
// One input plane to one output plane; all strides are in float elements.
// Explicit input padding belongs to the preceding pad operator. The caller
// guarantees that every 3x3 window is in bounds and that buffers do not alias.
// Only nine coefficients are staged from OSPI per output channel (36 bytes).
struct ConvPlane {
    const float *input;
    float *output;
    const float *weights;
    int input_y, input_x, output_y, output_x, weight_y, weight_x;
    int height, width, stride_y, stride_x;
    float bias;
};

inline void conv3x3_scalar(const ConvPlane &p) {
    float w[9];
    for (int k = 0; k < 9; ++k)
        w[k] = p.weights[(k / 3) * p.weight_y + (k % 3) * p.weight_x];
    for (int y = 0; y < p.height; ++y)
        for (int x = 0; x < p.width; ++x) {
            const float *src = p.input + y * p.stride_y * p.input_y + x * p.stride_x * p.input_x;
            float sum = 0;
            for (int k = 0; k < 9; ++k)
                sum += src[(k / 3) * p.input_y + (k % 3) * p.input_x] * w[k];
            p.output[y * p.output_y + x * p.output_x] = sum + p.bias;
        }
}

inline void conv3x3(const ConvPlane &p) {
#if YAMNET_MVE_F32
    float w[9];
    for (int k = 0; k < 9; ++k)
        w[k] = p.weights[(k / 3) * p.weight_y + (k % 3) * p.weight_x];
    const uint32x4_t in_offsets = vmulq_n_u32(vidupq_n_u32(0, 1), p.stride_x * p.input_x);
    const uint32x4_t out_offsets = vmulq_n_u32(vidupq_n_u32(0, 1), p.output_x);
    for (int y = 0; y < p.height; ++y) {
        const float *row = p.input + y * p.stride_y * p.input_y;
        float *dst = p.output + y * p.output_y;
        int x = 0;
        for (; x + 4 <= p.width; x += 4) {
            const float *src = row + x * p.stride_x * p.input_x;
            float32x4_t sum = vdupq_n_f32(0);
            for (int k = 0; k < 9; ++k) {
                const float *tap = src + (k / 3) * p.input_y + (k % 3) * p.input_x;
                const float32x4_t values = p.stride_x * p.input_x == 1
                    ? vld1q_f32(tap) : vldrwq_gather_shifted_offset_f32(tap, in_offsets);
                sum = vfmaq_n_f32(sum, values, w[k]);
            }
            sum = vaddq_n_f32(sum, p.bias);
            if (p.output_x == 1) vst1q_f32(dst + x, sum);
            else vstrwq_scatter_shifted_offset_f32(dst + x * p.output_x, out_offsets, sum);
        }
        // A scalar remainder never loads beyond a valid input window.
        for (; x < p.width; ++x) {
            const float *src = row + x * p.stride_x * p.input_x;
            float sum = 0;
            for (int k = 0; k < 9; ++k)
                sum += src[(k / 3) * p.input_y + (k % 3) * p.input_x] * w[k];
            dst[x * p.output_x] = sum + p.bias;
        }
    }
#else
    conv3x3_scalar(p);
#endif
}
} // namespace yamnet
