// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "app_params.h"
#include "datatypes.h"
#include <cstdint>
#if defined(_MSC_VER)
#define __PRETTY_FUNCTION__ __FUNCSIG__
#endif
extern uint32_t test_tick, test_iterations, test_limit;
inline uint32_t osKernelGetTickFreq() { return 1000; }
inline uint32_t osKernelGetTickCount() { return test_tick; }
inline uint32_t osKernelGetSysTimerCount() { return test_tick * 1000; }
inline uint32_t osKernelGetSysTimerFreq() { return 1000000; }
using osStatus_t = int;
enum { osOK = 0, osErrorParameter = -4 };
extern bool test_delay_race, test_delay_error;
inline int osDelayUntil(uint32_t due) {
    if (test_delay_error) return -1;
    test_tick = due;
    if (test_delay_race) { ++test_tick; return osErrorParameter; }
    return osOK;
}
inline int osDelay(uint32_t ticks) { test_tick += ticks; return osOK; }
inline int32_t osKernelLock() { return 0; }
inline int osKernelRestoreLock(int32_t) { return 0; }
#define CG_AFTER_ITERATION if (++test_iterations >= test_limit) { cgStaticError = CG_STOP_SCHEDULER; goto errorHandling; }
