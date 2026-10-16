// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdint.h>
typedef struct {
    volatile int32_t error;
    volatile uint32_t audio_blocks, audio_started, mfcc_frames, inferences, warmup_windows;
    volatile uint32_t audio_ticks_max, inference_ticks_max, audio_deadline_misses;
    volatile uint32_t frames_presented, lcd_deadline_misses;
    volatile uint32_t lcd_events_sent, lcd_events_received, lcd_events_dropped;
    volatile uint32_t audio_discontinuities, last_discontinuity_block;
    volatile uint32_t last_class, recognitions, displayed_class, display_alpha;
    float last_probability;
} StreamStats;
