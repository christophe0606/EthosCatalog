// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "waveform_display.hpp"
#include "app_sizes.h"
#include "board_nodes.h"
#include "cmsis_os2.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>

namespace dtln::waveform {
namespace {
constexpr unsigned width = 800, height = 480;
constexpr unsigned left = 32, plotWidth = 736, plotHeight = 166;
constexpr unsigned samplesPerBin = DTLN_AUDIO_SAMPLE_RATE / 1000;
constexpr unsigned binsPerHop = DTLN_AUDIO_HOP_SAMPLES / samplesPerBin;
constexpr unsigned delayHops = DTLN_AUDIO_FRAME_SAMPLES / DTLN_AUDIO_HOP_SAMPLES - 1;
constexpr unsigned historyBins = 512;
constexpr unsigned displayFps = 10;
constexpr uint32_t dataReadyFlag = 1;
static_assert(DTLN_AUDIO_HOP_SAMPLES % samplesPerBin == 0 && historyBins % binsPerHop == 0);
static_assert(DTLN_AUDIO_FRAME_SAMPLES % DTLN_AUDIO_HOP_SAMPLES == 0);
static_assert(width * height == LCD_PIXELS);

constexpr uint16_t background = 0x0863, grid = 0x222a, zeroLine = 0x3b0f;
constexpr uint16_t microphoneColor = 0x2e7f, outputColor = 0x67f2;

struct Peak {
    float low, high;
};

struct PairedPeak {
    Peak microphone, output;
};

struct PlotHistory {
    std::array<PairedPeak, historyBins> bins;
    unsigned next;
};

alignas(32) uint16_t frames[2][LCD_PIXELS] __attribute__((section(".bss.lcd_frame_buf")));
alignas(8) uint8_t displayStack[4096];

PlotHistory history{}, snapshot{};
std::array<std::array<Peak, binsPerHop>, delayHops + 1> microphoneDelay{};
unsigned microphoneSlot;
uint32_t lastOfferTick, offerPeriod;
osThreadId_t displayThread;
std::atomic<bool> ready{false}, snapshotPending{false};
std::atomic<uint32_t> presented{0};
std::atomic<int32_t> error{0};

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

uint16_t* column(uint16_t* frame, unsigned x)
{
    return frame + x * height;
}

void drawSpan(uint16_t* pixels, unsigned y, unsigned length, uint16_t color)
{
    // Logical y runs opposite to the native pixel order in this landscape orientation.
    std::fill_n(pixels + height - y - length, length, color);
}

void caption(uint16_t* frame, const char* text, unsigned y, uint16_t color)
{
    constexpr unsigned scale = 3;
    unsigned x = left;
    for (; *text; ++text, x += 6 * scale) {
        if (*text < 'A' || *text > 'Z') continue;
        for (unsigned c = 0; c < 5; ++c) {
            for (unsigned r = 0; r < 7; ++r) {
                if (letters[*text - 'A'][c] & (1U << r)) {
                    for (unsigned dx = 0; dx < scale; ++dx) {
                        drawSpan(column(frame, x + c * scale + dx), y + r * scale, scale, color);
                    }
                }
            }
        }
    }
}

void plot(uint16_t* frame, unsigned top, Peak PairedPeak::*trace, float scale, uint16_t color)
{
    const unsigned middle = top + plotHeight / 2;
    for (unsigned x = 0; x < plotWidth; ++x) {
        auto* pixels = column(frame, left + x);
        if (x % (plotWidth / 4) == 0 || x == plotWidth - 1)
            drawSpan(pixels, top, plotHeight + 1, grid);
        drawSpan(pixels, top, 1, grid);
        drawSpan(pixels, top + plotHeight, 1, grid);
        drawSpan(pixels, middle, 1, zeroLine);
        const auto peak = snapshot.bins[(snapshot.next + x * historyBins / plotWidth) % historyBins].*trace;
        const int high = static_cast<int>(middle) - static_cast<int>(peak.high * scale);
        const int low = static_cast<int>(middle) - static_cast<int>(peak.low * scale);
        drawSpan(pixels, high, low - high + 1, color);
    }
}

void render(uint16_t* frame)
{
    // Both traces use the same gain so echo reduction is visible.
    float maximum = 0.015625f;
    for (const auto& bin : snapshot.bins) {
        maximum = std::max({maximum, std::abs(bin.microphone.low), std::abs(bin.microphone.high),
                            std::abs(bin.output.low), std::abs(bin.output.high)});
    }
    const float scale = (plotHeight / 2 - 1) / maximum;
    std::fill_n(frame, LCD_PIXELS, background);
    caption(frame, "MICROPHONE", 23, microphoneColor);
    caption(frame, "ECHO CANCELLED", 253, outputColor);
    plot(frame, 54, &PairedPeak::microphone, scale, microphoneColor);
    plot(frame, 284, &PairedPeak::output, scale, outputColor);
}

void display(void*)
{
    render(frames[0]);
    const int initStatus = lcd_init_display(frames[0]);
    error.store(initStatus);
    if (initStatus != 0) return;
    ready.store(true, std::memory_order_release);

    unsigned back = 1;
    while ((osThreadFlagsWait(dataReadyFlag, osFlagsWaitAny, osWaitForever) & osFlagsError) == 0) {
        if (!snapshotPending.load(std::memory_order_acquire)) continue;
        render(frames[back]);
        // The snapshot is no longer needed while the controller scans out the frame.
        snapshotPending.store(false, std::memory_order_release);
        const int status = lcd_present(frames[back]);
        if (status != 0) {
            error.store(status);
            break;
        }
        presented.fetch_add(1, std::memory_order_relaxed);
        back ^= 1U;
    }
    ready.store(false, std::memory_order_release);
}

Peak envelope(const float* samples, unsigned stride)
{
    uint32_t bits;
    Peak peak{1.0f, -1.0f};
    for (unsigned i = 0; i < samplesPerBin; ++i) {
        float value = samples[i * stride];
        std::memcpy(&bits, &value, sizeof(bits));
        if ((bits & 0x7f800000U) == 0x7f800000U) value = 0.0f;
        value = std::clamp(value, -1.0f, 1.0f);
        peak.low = std::min(peak.low, value);
        peak.high = std::max(peak.high, value);
    }
    return peak;
}

void offerSnapshot(bool force)
{
    const uint32_t tick = osKernelGetTickCount();
    if (snapshotPending.load(std::memory_order_acquire) || (!force && tick - lastOfferTick < offerPeriod)) return;
    snapshot = history;
    lastOfferTick = tick;
    snapshotPending.store(true, std::memory_order_release);
    osThreadFlagsSet(displayThread, dataReadyFlag);
}

void observeMicrophone(void*, const void* record, uint32_t bytes)
{
    if (!ready.load(std::memory_order_acquire) || bytes != 2 * DTLN_AUDIO_HOP_SAMPLES * sizeof(float)) return;
    const auto* mic = static_cast<const float*>(record);
    for (unsigned bin = 0; bin < binsPerHop; ++bin)
        microphoneDelay[microphoneSlot][bin] = envelope(mic + 2 * bin * samplesPerBin, 2);
}

void observeOutput(void*, const void* record, uint32_t bytes)
{
    if (!ready.load(std::memory_order_acquire) || bytes != DTLN_AUDIO_HOP_SAMPLES * sizeof(float)) return;
    const auto* output = static_cast<const float*>(record);
    microphoneSlot = (microphoneSlot + 1) % microphoneDelay.size();
    for (unsigned bin = 0; bin < binsPerHop; ++bin) {
        history.bins[history.next] = {microphoneDelay[microphoneSlot][bin], envelope(output + bin * samplesPerBin, 1)};
        history.next = (history.next + 1) % historyBins;
    }
    offerSnapshot(false);
}
} // namespace

void start()
{
    offerPeriod = (osKernelGetTickFreq() + displayFps - 1) / displayFps;
    osThreadAttr_t attr{};
    attr.name = "DTLN waveform";
    attr.stack_mem = displayStack;
    attr.stack_size = sizeof(displayStack);
    attr.priority = osPriorityBelowNormal;
    displayThread = osThreadNew(display, nullptr, &attr);
    if (!displayThread) error.store(osErrorResource);
}

void reset()
{
    history = {};
    microphoneDelay = {};
    microphoneSlot = 0;
    lastOfferTick = osKernelGetTickCount() - offerPeriod;
}

void finish()
{
    if (ready.load(std::memory_order_acquire)) offerSnapshot(true);
}

SdsRecordObserver microphoneObserver() { return {observeMicrophone}; }
SdsRecordObserver outputObserver() { return {observeOutput}; }
uint32_t framesPresented() { return presented.load(std::memory_order_relaxed); }
int32_t displayError() { return error.load(); }
}
