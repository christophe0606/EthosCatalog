// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace encodec::waveform {

inline constexpr unsigned width = 800;
inline constexpr unsigned height = 128;
inline constexpr unsigned samplesPerBin = 24;

struct Peak {
    float low = 0.0f;
    float high = 0.0f;
};

struct History {
    std::array<Peak, 512> bins{};
    std::size_t next = 0;
    unsigned pendingSamples = 0;
    Peak pending;
};

void append(History& history, const float* samples, std::size_t count);
void render(uint16_t* frame, const History& history);

}
