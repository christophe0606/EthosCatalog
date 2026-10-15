// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdint.h>

// Packet metadata travels through the same dataflow FIFOs as the samples.
// No frame spanning a capture discontinuity may reach the display.
typedef struct {
    uint32_t started;
    uint32_t discontinuity;
    uint32_t valid;
    float amplitude_scale;
} StreamBlockInfo;

typedef struct {
    volatile int32_t error;
    volatile uint32_t audio_blocks, spectrum_frames, frames_presented;
    volatile uint32_t audio_ticks_max, audio_deadline_misses, lcd_deadline_misses;
    volatile uint32_t lcd_events_sent, lcd_events_received, lcd_events_dropped;
    volatile uint32_t discontinuities, warmup_blocks;
} StreamStats;
