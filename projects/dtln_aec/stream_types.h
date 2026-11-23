// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdint.h>

typedef struct {
    volatile uint32_t stage1_calls, stage2_calls;
    volatile uint32_t stage1_ticks_max, stage2_ticks_max;
    uint32_t hop_ticks_max, deadline_misses;
} StreamStats;
