// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstdint>

namespace dtln::waveform {
constexpr unsigned historyBins = 512;
// SDS user bits defined by test_data/datatest.sdsio.yml.
constexpr uint32_t farEndFlag = 1U << 0;
constexpr uint32_t nearEndFlag = 1U << 1;
constexpr uint32_t doubleTalkFlag = 1U << 2;
constexpr uint32_t talkFlags = farEndFlag | nearEndFlag | doubleTalkFlag;

struct Peak { float low, high; };
struct PairedPeak { Peak microphone, output; };
struct History {
    std::array<PairedPeak, historyBins> bins;
    unsigned next;
    uint32_t playbackFlags;
};
void reset(History& history);
void append(History& history, const float* microphone, const float* output);
void render(uint16_t* frame, const History& history);
}
