// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdint.h>
#include "stream_types.h"
#include "app_sizes.h"

typedef struct { uint32_t count; uint8_t bytes[32]; } UartBlock;
// current is an opaque pointer to the editor node's atomic generation counter.
// Sampling settings are captured with the text: changing UART settings cannot
// change the distribution of a sentence that is already being generated.
typedef struct {
    uint32_t generation;
    char text[76];
    void *current;
    float bias;
    uint32_t seed;
} SentenceSnapshot;
