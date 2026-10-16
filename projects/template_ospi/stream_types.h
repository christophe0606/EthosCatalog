// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdint.h>

// Diagnostics for this application's audio-to-spectrum pipeline.
typedef struct {
    volatile int32_t error;
    volatile uint32_t audio_blocks, spectrum_frames, frames_presented;
    volatile uint32_t audio_ticks_max, audio_deadline_misses, lcd_deadline_misses;
    volatile uint32_t lcd_events_sent, lcd_events_received, lcd_events_dropped;
    // Diagnostic timestamp of the latest source firing, outside the dataflow.
    volatile uint32_t audio_started;
} StreamStats;
