// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cmath>
#include <cstdint>

namespace dtln {

// Periodic equivalent of model/test_patterns.py. The last second of silence
// lets the 104-sample echo path decay before the next period begins.
class TestPattern {
public:
    static constexpr uint32_t sample_rate = 16000;
    static constexpr uint32_t cycle_samples = 5 * sample_rate;

    struct Sample {
        float microphone;
        float reference;
        float clean;
    };

    TestPattern()
    {
        for (uint32_t i = 0; i < far_.size(); ++i) {
            const double phase = tau * i / far_.size();
            far_[i] = static_cast<float>(
                0.16 * std::sin(phase) + 0.09 * std::sin(2.0 * phase) +
                0.05 * std::sin(4.0 * phase) + 0.025 * std::sin(7.0 * phase));
        }
        for (uint32_t i = 0; i < near_.size(); ++i) {
            const double phase = tau * i / near_.size();
            near_[i] = static_cast<float>(
                0.13 * std::sin(phase) + 0.06 * std::sin(3.0 * phase) +
                0.04 * std::sin(4.0 * phase) + 0.025 * std::sin(8.0 * phase));
        }
    }

    Sample at(uint32_t index) const
    {
        index %= cycle_samples;
        const float clean = near_sample(index);
        const float echo = 0.58f * far_sample(delayed(index, 53)) +
                           0.21f * far_sample(delayed(index, 104));
        return {clean + echo, far_sample(index), clean};
    }

private:
    static constexpr double tau = 6.28318530717958647692;
    std::array<float, 127> far_{};
    std::array<float, 89> near_{};

    static uint32_t delayed(uint32_t index, uint32_t delay)
    {
        return (index + cycle_samples - delay) % cycle_samples;
    }

    static float envelope(uint32_t index)
    {
        const float position = static_cast<float>(index % 4096);
        return 0.4f + 0.6f * (1.0f - std::fabs(position - 2048.0f) / 2048.0f);
    }

    float far_sample(uint32_t index) const
    {
        if (index < sample_rate || index >= 3 * sample_rate) {
            return 0.0f;
        }
        return far_[index % far_.size()] * envelope(index);
    }

    float near_sample(uint32_t index) const
    {
        if (index < 2 * sample_rate || index >= 4 * sample_rate) {
            return 0.0f;
        }
        return near_[index % near_.size()] * envelope(index);
    }
};

} // namespace dtln
