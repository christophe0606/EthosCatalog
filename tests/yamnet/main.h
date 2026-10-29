// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdint.h>
// Independent mock of the board PMU interface for portable node tests.
typedef struct {uint64_t cycles,active_cycles;uint32_t invocations,overflow;} ethos_benchmark_t;
extern volatile ethos_benchmark_t ethos_benchmark;
void ethos_benchmark_reset(void);
