// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

#if defined(__ARM_FEATURE_MVE) && (__ARM_FEATURE_MVE & 2)
#include <arm_mve.h>
#endif

namespace encodec::cpu {

inline constexpr unsigned eluSamplesPerUnit = 64;
inline constexpr unsigned eluCutoff = 18;
using EluTable = std::array<float, eluCutoff * eluSamplesPerUnit + 1>;

inline void fillEluTable(EluTable& table)
{
    for (std::size_t i = 0; i < table.size(); ++i) {
        table[i] = std::expm1(-static_cast<float>(i) / eluSamplesPerUnit);
    }
}

inline float approximateElu(float input, const EluTable& table)
{
    if (!(input < 0.0f)) return input;
    if (input <= -static_cast<float>(eluCutoff)) return -1.0f;
    const float position = -input * eluSamplesPerUnit;
    const auto index = static_cast<unsigned>(position);
    const float remainder = (static_cast<float>(index) - position) / eluSamplesPerUnit;
    const float base = table[index];
    const float adjustment = remainder * (1.0f + remainder * (0.5f + remainder / 6.0f));
    return base + (1.0f + base) * adjustment;
}

inline void approximateElu(const float* input, float* output, std::size_t count, const EluTable& table)
{
    std::size_t i = 0;
#if defined(__ARM_FEATURE_MVE) && (__ARM_FEATURE_MVE & 2)
    constexpr float cellWidth = 1.0f / eluSamplesPerUnit;
    for (; count - i >= 4; i += 4) {
        const auto x = vld1q_f32(input + i);
        const auto negative = vcmpltq_n_f32(x, 0.0f);
        if (negative == 0) {
            vst1q_f32(output + i, x);
            continue;
        }
        const auto magnitude = vmaxnmq_f32(vdupq_n_f32(0.0f),
                                           vminnmq_f32(vnegq_f32(x), vdupq_n_f32(eluCutoff)));
        const auto position = vmulq_n_f32(magnitude, eluSamplesPerUnit);
        const auto index = vcvtq_u32_f32(position);
        const auto remainder = vmulq_n_f32(vsubq_f32(vcvtq_f32_u32(index), position), cellWidth);
        const auto base = vldrwq_gather_shifted_offset_f32(table.data(), index);
        const auto quadratic = vaddq_f32(vdupq_n_f32(0.5f), vmulq_n_f32(remainder, 1.0f / 6.0f));
        const auto adjustment = vmulq_f32(remainder,
            vaddq_f32(vdupq_n_f32(1.0f), vmulq_f32(remainder, quadratic)));
        const auto result = vaddq_f32(base, vmulq_f32(vaddq_f32(vdupq_n_f32(1.0f), base), adjustment));
        vst1q_f32(output + i, vpselq_f32(result, x, negative));
    }
#endif
    for (; i < count; ++i) output[i] = approximateElu(input[i], table);
}

}
