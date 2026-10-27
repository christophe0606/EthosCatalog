// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdint.h>
typedef struct {
    volatile int32_t error;
    volatile uint32_t uart_bytes,requests,acknowledgments,rejected,event_drops,input_editing;
    volatile uint32_t frames,played_blocks,lcd_frames,lcd_skipped,audio_underruns;
    volatile uint32_t frontend_ticks,neural_ticks,pitch_ticks,vocoder_ticks,synthesis_ticks;
    volatile uint32_t first_audio_ticks,deadline_misses,lcd_ticks_max,pcm_peak,pcm_nonzero;
    volatile uint32_t utterance_started,audio_callbacks,completed_blocks,audio_interval_min,audio_interval_max;
} StreamStats;
