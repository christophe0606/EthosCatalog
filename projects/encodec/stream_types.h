// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdint.h>

typedef struct {
    volatile uint32_t completed_blocks;
    volatile uint32_t encoder_blocks;
    volatile uint32_t decoder_blocks;
    volatile uint32_t encoder_ticks_max;
    volatile uint32_t decoder_ticks_max;
    volatile uint32_t encoder_deadline_misses;
    volatile uint32_t decoder_deadline_misses;
    volatile uint32_t codec_ticks_max;
    volatile uint32_t codec_deadline_misses;
    volatile uint32_t replay_deadline_misses;
    volatile uint32_t lcd_frames;
    volatile uint32_t waveform_drops;
    volatile uint32_t timer_hz;
    volatile uint32_t codec_budget_ticks;
    volatile int32_t codec_error;
    volatile int32_t stream_error;
    volatile int32_t replay_status;
    volatile int32_t record_status;
    volatile int32_t lcd_error;
    volatile int32_t waveform_drain_error;
    volatile int32_t transport_error;
    volatile uint32_t control_stop_flags;
} EnCodecDemoStats;
