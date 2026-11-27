// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "waveform.hpp"

#include "board_nodes.h"

#include <algorithm>
#include <cstring>

namespace encodec::waveform {
namespace {

constexpr unsigned left = 32, plotWidth = width - 2 * left;
constexpr unsigned top = 12, bottom = height - top - 1;
constexpr unsigned middle = height / 2;
constexpr float amplitude = 48.0f;
constexpr uint16_t background = 0x0863, grid = 0x222a, zeroLine = 0x3b0f, trace = 0x2e7f;
static_assert(width == 800 && height <= LCD_PIXELS / width && height % 16 == 0);
static_assert(sizeof(float) == sizeof(uint32_t));

void drawSpan(uint16_t* column, unsigned first, unsigned last, uint16_t color)
{
    // The panel stores each landscape column with its bottom pixel first.
    std::fill_n(column + height - 1 - last, last - first + 1, color);
}

}

void append(History& history, const float* samples, std::size_t count)
{
    for (std::size_t index = 0; index < count; ++index) {
        float sample = samples[index];
        uint32_t bits;
        std::memcpy(&bits, &sample, sizeof(bits));
        if ((bits & 0x7f800000U) == 0x7f800000U) sample = 0.0f;
        sample = std::clamp(sample, -1.0f, 1.0f);
        if (history.pendingSamples == 0) {
            history.pending = {sample, sample};
        } else {
            history.pending.low = std::min(history.pending.low, sample);
            history.pending.high = std::max(history.pending.high, sample);
        }
        if (++history.pendingSamples == samplesPerBin) {
            history.bins[history.next] = history.pending;
            history.next = (history.next + 1) % history.bins.size();
            history.pendingSamples = 0;
        }
    }
}

void render(uint16_t* frame, const History& history)
{
    std::fill_n(frame, width * height, background);
    for (unsigned x = 0; x < plotWidth; ++x) {
        auto* column = frame + (left + x) * height;
        if (x % (plotWidth / 4) == 0 || x == plotWidth - 1) {
            drawSpan(column, top, bottom, grid);
        }
        drawSpan(column, top, top, grid);
        drawSpan(column, middle, middle, zeroLine);
        drawSpan(column, bottom, bottom, grid);
        const auto offset = x * history.bins.size() / plotWidth;
        const auto& peak = history.bins[(history.next + offset) % history.bins.size()];
        const int first = middle - static_cast<int>(peak.high * amplitude);
        const int last = middle - static_cast<int>(peak.low * amplitude);
        drawSpan(column, first, last, trace);
    }
}

}
