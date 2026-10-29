// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdint.h>
typedef struct {
 volatile int32_t error;
 volatile uint32_t audio_blocks, audio_discontinuities, last_discontinuity_block;
 volatile uint32_t audio_started, audio_ticks_max, audio_deadline_misses;
 volatile uint32_t inferences, inference_ticks_max, state_resets;
 volatile uint32_t detections, last_detection_tick;
 volatile float last_probability;
 volatile uint32_t frames_presented, display_alpha, fade_restarts, fade_expiries;
 volatile uint32_t lcd_events_sent, lcd_events_received, lcd_events_dropped, lcd_deadline_misses;
} StreamStats;
