// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "waveform_display.hpp"
#include "app_sizes.h"
#include "peripherals.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace dtln::waveform {
namespace {
constexpr unsigned width = DtlnLcd::width, height = DtlnLcd::height;
constexpr unsigned pixelCount = width * height;
constexpr unsigned left = 32, plotWidth = 736, plotHeight = 166;
constexpr unsigned outputTop = 284, playbackCaptionTop = 459, playbackCaptionScale = 2;
static_assert(playbackCaptionTop > outputTop + plotHeight && playbackCaptionTop + 7 * playbackCaptionScale <= height);
constexpr unsigned samplesPerBin = DTLN_AUDIO_SAMPLE_RATE / 1000;
constexpr unsigned binsPerHop = DTLN_AUDIO_HOP_SAMPLES / samplesPerBin;
constexpr unsigned delayHops = DTLN_AUDIO_FRAME_SAMPLES / DTLN_AUDIO_HOP_SAMPLES - 1;
static_assert(DTLN_AUDIO_HOP_SAMPLES % samplesPerBin == 0 && historyBins % binsPerHop == 0);
static_assert(DTLN_AUDIO_FRAME_SAMPLES % DTLN_AUDIO_HOP_SAMPLES == 0);

constexpr uint16_t background = 0x0863, grid = 0x222a, zeroLine = 0x3b0f;
constexpr uint16_t microphoneColor = 0x2e7f, outputColor = 0x67f2;
constexpr uint16_t playbackCaptionColor = 0xffff;

std::array<std::array<Peak, binsPerHop>, delayHops + 1> microphoneDelay{};
unsigned microphoneSlot;

// 5x7 letter shapes follow the repository's keyword UI font.
constexpr uint8_t letters[26][5] = {
    {0x7e,0x11,0x11,0x11,0x7e},{0x7f,0x49,0x49,0x49,0x36},
    {0x3e,0x41,0x41,0x41,0x22},{0x7f,0x41,0x41,0x22,0x1c},
    {0x7f,0x49,0x49,0x49,0x41},{0x7f,0x09,0x09,0x09,0x01},
    {0x3e,0x41,0x49,0x49,0x7a},{0x7f,0x08,0x08,0x08,0x7f},
    {0x00,0x41,0x7f,0x41,0x00},{0x20,0x40,0x41,0x3f,0x01},
    {0x7f,0x08,0x14,0x22,0x41},{0x7f,0x40,0x40,0x40,0x40},
    {0x7f,0x02,0x0c,0x02,0x7f},{0x7f,0x04,0x08,0x10,0x7f},
    {0x3e,0x41,0x41,0x41,0x3e},{0x7f,0x09,0x09,0x09,0x06},
    {0x3e,0x41,0x51,0x21,0x5e},{0x7f,0x09,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7f,0x01,0x01},
    {0x3f,0x40,0x40,0x40,0x3f},{0x1f,0x20,0x40,0x20,0x1f},
    {0x3f,0x40,0x38,0x40,0x3f},{0x63,0x14,0x08,0x14,0x63},
    {0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43}
};

constexpr char playbackCharacters[] = "-abdefklnortu";
constexpr uint8_t playbackGlyphs[][5] = {
    {0x08, 0x08, 0x08, 0x08, 0x08}, // -
    {0x20, 0x54, 0x54, 0x54, 0x78}, // a
    {0x7f, 0x48, 0x44, 0x44, 0x38}, // b
    {0x38, 0x44, 0x44, 0x48, 0x7f}, // d
    {0x38, 0x54, 0x54, 0x54, 0x18}, // e
    {0x08, 0x7e, 0x09, 0x01, 0x02}, // f
    {0x7f, 0x10, 0x28, 0x44, 0x40}, // k
    {0x00, 0x41, 0x7f, 0x40, 0x00}, // l
    {0x7c, 0x08, 0x04, 0x04, 0x78}, // n
    {0x38, 0x44, 0x44, 0x44, 0x38}, // o
    {0x7c, 0x08, 0x04, 0x04, 0x08}, // r
    {0x04, 0x3f, 0x44, 0x40, 0x20}, // t
    {0x3c, 0x40, 0x40, 0x20, 0x7c}, // u
};
static_assert(std::size(playbackGlyphs) + 1 == sizeof(playbackCharacters));

uint16_t* column(uint16_t* frame, unsigned x)
{
    return frame + x * height;
}

void drawSpan(uint16_t* pixels, unsigned y, unsigned length, uint16_t color)
{
    // Logical y runs opposite to the native pixel order in this landscape orientation.
    std::fill_n(pixels + height - y - length, length, color);
}

void caption(uint16_t* frame, const char* text, unsigned y, uint16_t color, unsigned scale = 3)
{
    unsigned x = left;
    for (; *text; ++text, x += 6 * scale) {
        const uint8_t* glyph;
        if (*text >= 'A' && *text <= 'Z') {
            glyph = letters[*text - 'A'];
        } else {
            const auto* match = std::strchr(playbackCharacters, *text);
            if (!match) continue;
            glyph = playbackGlyphs[match - playbackCharacters];
        }
        for (unsigned c = 0; c < 5; ++c) {
            for (unsigned r = 0; r < 7; ++r) {
                if (glyph[c] & (1U << r)) {
                    for (unsigned dx = 0; dx < scale; ++dx) {
                        drawSpan(column(frame, x + c * scale + dx), y + r * scale, scale, color);
                    }
                }
            }
        }
    }
}

const char* playbackCaption(uint32_t flags)
{
    switch (flags & talkFlags) {
    case farEndFlag: return "far-end";
    case nearEndFlag: return "near-end";
    case doubleTalkFlag: return "double talk";
    default: return "";
    }
}

void plot(uint16_t* frame, const History& history, unsigned top,
          Peak PairedPeak::*trace, float scale, uint16_t color)
{
    const unsigned middle = top + plotHeight / 2;
    for (unsigned x = 0; x < plotWidth; ++x) {
        auto* pixels = column(frame, left + x);
        if (x % (plotWidth / 4) == 0 || x == plotWidth - 1)
            drawSpan(pixels, top, plotHeight + 1, grid);
        drawSpan(pixels, top, 1, grid);
        drawSpan(pixels, top + plotHeight, 1, grid);
        drawSpan(pixels, middle, 1, zeroLine);
        const auto peak = history.bins[(history.next + x * historyBins / plotWidth) % historyBins].*trace;
        const int high = static_cast<int>(middle) - static_cast<int>(peak.high * scale);
        const int low = static_cast<int>(middle) - static_cast<int>(peak.low * scale);
        drawSpan(pixels, high, low - high + 1, color);
    }
}

void paint(uint16_t* frame, const History& history)
{
    // Both traces use the same gain so echo reduction is visible.
    float maximum = 0.015625f;
    for (const auto& bin : history.bins) {
        maximum = std::max({maximum, std::abs(bin.microphone.low), std::abs(bin.microphone.high),
                            std::abs(bin.output.low), std::abs(bin.output.high)});
    }
    const float scale = (plotHeight / 2 - 1) / maximum;
    std::fill_n(frame, pixelCount, background);
    caption(frame, "MICROPHONE", 23, microphoneColor);
    caption(frame, "ECHO CANCELLED", 253, outputColor);
    plot(frame, history, 54, &PairedPeak::microphone, scale, microphoneColor);
    plot(frame, history, outputTop, &PairedPeak::output, scale, outputColor);
    caption(frame, playbackCaption(history.playbackFlags), playbackCaptionTop, playbackCaptionColor, playbackCaptionScale);
}

Peak envelope(const float* samples)
{
    uint32_t bits;
    Peak peak{1.0f, -1.0f};
    for (unsigned i = 0; i < samplesPerBin; ++i) {
        float value = samples[i];
        std::memcpy(&bits, &value, sizeof(bits));
        if ((bits & 0x7f800000U) == 0x7f800000U) value = 0.0f;
        value = std::clamp(value, -1.0f, 1.0f);
        peak.low = std::min(peak.low, value);
        peak.high = std::max(peak.high, value);
    }
    return peak;
}

} // namespace

void append(History& history, const float* microphone, const float* output)
{
    for (unsigned bin = 0; bin < binsPerHop; ++bin) {
        microphoneDelay[microphoneSlot][bin] = envelope(microphone + bin * samplesPerBin);
    }
    microphoneSlot = (microphoneSlot + 1) % microphoneDelay.size();
    for (unsigned bin = 0; bin < binsPerHop; ++bin) {
        history.bins[history.next] = {microphoneDelay[microphoneSlot][bin], envelope(output + bin * samplesPerBin)};
        history.next = (history.next + 1) % historyBins;
    }
}

void reset(History& history)
{
    history = {};
    microphoneDelay = {};
    microphoneSlot = 0;
}

void render(uint16_t* frame, const History& history)
{
    paint(frame, history);
}
}
