// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdint.h>

// Observable diagnostics. Synchronization is owned by the nodes, not these counters.
typedef struct {
    volatile int32_t error;
    volatile uint32_t uart_bytes, sentences, rejected, normalized, generation;
    volatile uint32_t requests, acknowledgments, inferences, generated, drawn;
    volatile uint32_t clears, frames_presented, wraps, lines, screen_full;
    volatile uint32_t stale_results, completed, step_limits, event_drops;
    volatile uint32_t source_ticks_max, source_deadline_misses, forward_ticks_max;
    volatile uint32_t lcd_ticks_max, lcd_deadline_misses;
    volatile uint32_t ink_overflows;
    volatile uint32_t sentence_started_ms, sentence_elapsed_ms, sentence_points;
    volatile uint32_t lcd_wait_ticks_max;
    volatile uint64_t forward_ticks_sum, lcd_ticks_sum;
    volatile uint64_t lcd_copy_bytes, lcd_clean_bytes;
} StreamStats;
