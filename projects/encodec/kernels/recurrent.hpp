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

enum class Gate { sigmoid, tanh };

inline constexpr unsigned gateSamplesPerUnit = 64;

template<Gate G>
inline constexpr unsigned gateCutoff = G == Gate::sigmoid ? 24 : 10;

template<Gate G>
using GateTable = std::array<float, gateCutoff<G> * gateSamplesPerUnit + 1>;

template<Gate G>
void fillGateTable(GateTable<G>& table)
{
    for (std::size_t i = 0; i < table.size(); ++i) {
        const double input = static_cast<double>(i) / gateSamplesPerUnit;
        if constexpr (G == Gate::sigmoid) {
            table[i] = static_cast<float>(1.0 / (1.0 + std::exp(input)));
        } else {
            table[i] = static_cast<float>(std::tanh(input));
        }
    }
}

template<Gate G>
float approximateGate(float input, const GateTable<G>& table)
{
    if (std::isnan(input)) return input;
    const float magnitude = std::abs(input);
    if (magnitude >= gateCutoff<G>) {
        if constexpr (G == Gate::sigmoid) return input < 0 ? 0.0f : 1.0f;
        return std::copysign(1.0f, input);
    }

    // Use a third-order Taylor polynomial about the lower table sample.
    const float position = magnitude * gateSamplesPerUnit;
    const auto index = static_cast<unsigned>(position);
    const float step = (position - static_cast<float>(index)) / gateSamplesPerUnit;
    const float base = table[index];
    if constexpr (G == Gate::sigmoid) {
        const float slope = base * (base - 1.0f);
        const float quadratic = (base - 0.5f) + step * (slope + 1.0f / 6.0f);
        const float result = std::fma(slope * step, std::fma(step, quadratic, 1.0f), base);
        return input < 0 ? result : 1.0f - result;
    } else {
        const float square = base * base;
        const float quadratic = -base + step * (square - 1.0f / 3.0f);
        const float result = std::fma((1.0f - square) * step, std::fma(step, quadratic, 1.0f), base);
        return std::copysign(result, input);
    }
}

template<Gate G>
void approximateGate(const float* input, float* output, std::size_t count, const GateTable<G>& table)
{
    std::size_t i = 0;
#if defined(__ARM_FEATURE_MVE) && (__ARM_FEATURE_MVE & 2)
    const auto one = vdupq_n_f32(1.0f);
    const auto cutoff = vdupq_n_f32(gateCutoff<G>);
    for (; count - i >= 4; i += 4) {
        const auto x = vld1q_f32(input + i);
        const auto magnitude = vabsq_f32(x);
        // Number-min also makes a NaN lane safe to convert and gather.
        const auto position = vmulq_n_f32(vminnmq_f32(magnitude, cutoff), gateSamplesPerUnit);
        const auto index = vcvtq_u32_f32(position);
        const auto step = vmulq_n_f32(vsubq_f32(position, vcvtq_f32_u32(index)), 1.0f / gateSamplesPerUnit);
        const auto base = vldrwq_gather_shifted_offset_f32(table.data(), index);
        float32x4_t result;
        if constexpr (G == Gate::sigmoid) {
            const auto slope = vmulq_f32(base, vsubq_f32(base, one));
            const auto quadratic = vaddq_f32(vsubq_f32(base, vdupq_n_f32(0.5f)),
                vmulq_f32(step, vaddq_f32(slope, vdupq_n_f32(1.0f / 6.0f))));
            const auto polynomial = vfmaq_f32(one, step, quadratic);
            result = vfmaq_f32(base, vmulq_f32(slope, step), polynomial);
            result = vpselq_f32(result, vdupq_n_f32(0.0f), vcmpltq_f32(magnitude, cutoff));
            result = vpselq_f32(result, vsubq_f32(one, result), vcmpltq_n_f32(x, 0.0f));
        } else {
            const auto square = vmulq_f32(base, base);
            const auto quadratic = vaddq_f32(vnegq_f32(base),
                vmulq_f32(step, vsubq_f32(square, vdupq_n_f32(1.0f / 3.0f))));
            const auto polynomial = vfmaq_f32(one, step, quadratic);
            result = vfmaq_f32(base, vmulq_f32(vsubq_f32(one, square), step), polynomial);
            const auto sign = vandq_u32(vreinterpretq_u32_f32(x), vdupq_n_u32(0x80000000U));
            result = vreinterpretq_f32_u32(vorrq_u32(vreinterpretq_u32_f32(result), sign));
        }
        vst1q_f32(output + i, vpselq_f32(result, x, vcmpeqq_f32(x, x)));
    }
#endif
    for (; i < count; ++i) output[i] = approximateGate<G>(input[i], table);
}

}
