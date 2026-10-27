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
    volatile uint32_t vocoder_frames,vocoder_frame_ticks_min,vocoder_frame_ticks_max,vocoder_init_ticks;
    volatile uint64_t vocoder_ticks64,synthesis_ticks64;
    volatile uint32_t generated_blocks,pcm_queue_max,pcm_queue_waits,pcm_hash,played_hash;
    volatile uint32_t generation_done_ticks,playback_starvations;
} StreamStats;
