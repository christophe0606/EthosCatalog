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

namespace encodec::cpu {

enum class Binary { add, subtract, multiply };

template<Binary Op>
float binary(float a, float b)
{
    if constexpr (Op == Binary::multiply) {
        return a * b;
    } else if constexpr (Op == Binary::subtract) {
        return a - b;
    } else {
        return a + b;
    }
}

template<Binary Op, bool Swap = false, bool Scalar = false>
void binaryLoop(const float* full, const float* operand, float* out, std::size_t count)
{
    const float scalar = Scalar && count != 0 ? operand[0] : 0.0f;
    for (std::size_t i = 0; i < count; ++i) {
        const float x = full[i];
        const float y = Scalar ? scalar : operand[i];
        out[i] = Swap ? binary<Op>(y, x) : binary<Op>(x, y);
    }
}

inline uint32_t floatBits(float value)
{
    uint32_t bits;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

inline int64_t argmaxRow(const float* input, std::size_t width)
{
#if defined(__ARM_FEATURE_MVE) && (__ARM_FEATURE_MVE & 2)
    if (width >= 4 && width % 4 == 0) {
        const auto hasNaN = [](float32x4_t values) {
            const auto magnitude = vandq_u32(vreinterpretq_u32_f32(values), vdupq_n_u32(0x7fffffffU));
            return vcmphiq_n_u32(magnitude, 0x7f800000U) != 0;
        };
        auto maximum = vld1q_f32(input);
        uint32x4_t indices{0, 1, 2, 3};
        auto positions = indices;
        if (!hasNaN(maximum)) {
            std::size_t i = 4;
            for (; i < width; i += 4) {
                const auto values = vld1q_f32(input + i);
                if (hasNaN(values)) break;
                positions = vaddq_n_u32(positions, 4);
                const auto better = vcmpgtq_f32(values, maximum);
                maximum = vpselq_f32(values, maximum, better);
                indices = vpselq_u32(positions, indices, better);
            }
            if (i == width) {
                float laneValues[4];
                uint32_t laneIndices[4];
                vst1q_f32(laneValues, maximum);
                vst1q_u32(laneIndices, indices);
                unsigned best = 0;
                for (unsigned lane = 1; lane < 4; ++lane) {
                    if (laneValues[lane] > laneValues[best] ||
                        (laneValues[lane] == laneValues[best] && laneIndices[lane] < laneIndices[best])) {
                        best = lane;
                    }
                }
                return laneIndices[best];
            }
        }
    }
#endif
    std::size_t best = 0;
    float maximum = input[0];
    if ((floatBits(maximum) & 0x7fffffffU) > 0x7f800000U) return 0;
    for (std::size_t i = 1; i < width; ++i) {
        const float value = input[i];
        // Match portable argmax: select the first NaN and keep the first tie.
        if ((floatBits(value) & 0x7fffffffU) > 0x7f800000U) return static_cast<int64_t>(i);
        if (value > maximum) {
            maximum = value;
            best = i;
        }
    }
    return static_cast<int64_t>(best);
}

// The caller verifies a zero point of zero and nearest-even rounding. The
// pinned portable kernel multiplies by a float reciprocal, then uses nearbyint.
inline bool quantizeShort(const float* input, int16_t* output, std::size_t count,
                          float inverseScale, int16_t low, int16_t high)
{
    constexpr uint32_t safeMagnitude = 0x5e800000U; // 2^62, within int64_t range
    std::size_t i = 0;
#if defined(__ARM_FEATURE_MVE) && (__ARM_FEATURE_MVE & 2)
    const auto lower = vdupq_n_f32(float(low));
    const auto upper = vdupq_n_f32(float(high));
    for (; count - i >= 4; i += 4) {
        const auto scaled = vmulq_n_f32(vld1q_f32(input + i), inverseScale);
        const auto magnitude = vandq_u32(vreinterpretq_u32_f32(scaled), vdupq_n_u32(0x7fffffffU));
        if (vcmphiq_n_u32(magnitude, safeMagnitude) != 0) return false;
        // Clamping to integral limits commutes with rounding and keeps the
        // narrowing store in range. NaN and infinity take the native fallback.
        const auto bounded = vminnmq_f32(vmaxnmq_f32(scaled, lower), upper);
        vstrhq_s32(output + i, vcvtnq_s32_f32(bounded));
    }
#endif
    for (; i < count; ++i) {
        const float scaled = input[i] * inverseScale;
        if ((floatBits(scaled) & 0x7fffffffU) > safeMagnitude) return false;
        float bounded = scaled;
        if (bounded < low) bounded = float(low);
        if (bounded > high) bounded = float(high);
        output[i] = static_cast<int16_t>(std::nearbyint(bounded));
    }
    return true;
}

}
