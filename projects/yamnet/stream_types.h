// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdint.h>
typedef struct {
    volatile int32_t error;
    volatile uint32_t audio_blocks, feature_frames, patches, coalesced_windows;
    volatile uint32_t inference_requests, inferences, frames_presented, request_errors;
    volatile uint32_t audio_started, audio_ticks_max, audio_deadline_misses;
    volatile uint32_t audio_stack_free, inference_stack_free;
    volatile uint32_t forward_ticks, npu_cycles, discontinuities;
} StreamStats;
