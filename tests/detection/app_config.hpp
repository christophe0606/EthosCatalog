// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstdint>
#include "datatypes.h"
#include "app_params.h"
extern uint32_t test_tick;
inline uint32_t osKernelGetTickCount() { return test_tick; }
inline uint32_t osKernelGetTickFreq() { return 1000; }
inline uint32_t osKernelGetSysTimerCount() { return test_tick*1000; }
inline uint32_t osKernelGetSysTimerFreq() { return 1000000; }
inline void osDelay(uint32_t ticks) { test_tick+=ticks; }
