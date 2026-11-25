// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstdint>

namespace dtln::waveform {
constexpr unsigned historyBins = 512;

struct Peak { float low, high; };
struct PairedPeak { Peak microphone, output; };
struct History {
    std::array<PairedPeak, historyBins> bins;
    unsigned next;
};
void reset(History& history);
void append(History& history, const float* microphone, const float* output);
void render(uint16_t* frame, const History& history);
}
