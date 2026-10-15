// Host time source; the production nodes and Stream event types are unchanged.
#pragma once
#include <cstdint>
#include "datatypes.h"
#include "app_params.h"
extern uint32_t test_tick;
inline uint32_t osKernelGetTickCount() { return test_tick; }
inline uint32_t osKernelGetTickFreq() { return 1000; }
inline uint32_t osKernelGetSysTimerCount() { return test_tick*1000; }
inline uint32_t osKernelGetSysTimerFreq() { return 1000000; }
