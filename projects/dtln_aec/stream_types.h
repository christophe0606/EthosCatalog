// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdint.h>

typedef struct {
    uint32_t samples;
    float input_rms, output_rms, residual_rms, clean_rms;
} DtlnCaseMetrics;

typedef struct {
    volatile int32_t error;
    // The source advances this on the dataflow thread at a new input episode.
    uint32_t input_epoch;
    volatile uint32_t audio_blocks, output_blocks, output_samples;
    volatile uint32_t stage1_calls, stage2_calls;
    volatile uint32_t stage1_ticks_max, stage2_ticks_max;
    volatile uint32_t processing_started, processing_ticks_max, deadline_misses;
    volatile uint32_t source_late_frames, cycles_completed, nonfinite_samples;
    volatile uint32_t cycle_checks, cycle_failures;
    volatile uint32_t stats_sequence;
    DtlnCaseMetrics cases[5];
} StreamStats;
