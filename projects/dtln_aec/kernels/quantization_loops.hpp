// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#if defined(__ARM_FEATURE_MVE) && (__ARM_FEATURE_MVE & 2)
#include <arm_mve.h>
#endif

namespace dtln::kernels {
// The caller checks contiguous Float/Short tensors, zero point zero, matching
// static shapes, valid Short limits, positive finite scale, and nearest-even
// rounding. Scale conversion and multiplication match ExecuTorch 1.4.1.
inline bool quantize_short(const float *input, int16_t *output, size_t count,
                           float inverse_scale, int16_t low, int16_t high) {
    size_t i = 0;
#if defined(__ARM_FEATURE_MVE) && (__ARM_FEATURE_MVE & 2)
    const auto lower = vdupq_n_f32(float(low));
    const auto upper = vdupq_n_f32(float(high));
    for (; i+4 <= count; i += 4) {
        const auto scaled = vmulq_n_f32(vld1q_f32(input+i), inverse_scale);
        // NaN, infinity and values outside a safe native int64 conversion
        // range use the original kernel. Partial output is then overwritten.
        const auto magnitude_bits = vandq_u32(vreinterpretq_u32_f32(scaled),
                                               vdupq_n_u32(0x7fffffffU));
        if (vcmphiq_n_u32(magnitude_bits,0x5e800000U) != 0)
            return false;
        // Integral limits commute with nearest-even rounding. Clamping first
        // makes the signed32 conversion and narrowing store safe.
        const auto bounded = vminnmq_f32(vmaxnmq_f32(scaled,lower),upper);
        vstrhq_s32(output+i,vcvtnq_s32_f32(bounded));
    }
#endif
    for (; i < count; ++i) {
        const float scaled = input[i]*inverse_scale;
        uint32_t bits;
        std::memcpy(&bits,&scaled,sizeof(bits));
        // Bit check also works when the target compiler uses fast-math.
        if ((bits & 0x7fffffffU) > 0x5e800000U) return false; // abs > 2^62
        const float bounded = scaled < low ? float(low) : scaled > high ? float(high) : scaled;
        output[i] = int16_t(std::nearbyint(bounded));
    }
    return true;
}
inline void dequantize_short(const int16_t *input, float *output, size_t count,
                             float scale) {
    size_t i = 0;
#if defined(__ARM_FEATURE_MVE) && (__ARM_FEATURE_MVE & 2)
    for (; i+4 <= count; i += 4)
        vstrwq_f32(output+i,vmulq_n_f32(vcvtq_f32_s32(vldrhq_s32(input+i)),scale));
#endif
    for (; i < count; ++i) output[i] = float(input[i])*scale;
}
} // namespace dtln::kernels
