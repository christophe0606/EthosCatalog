// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstdint>

namespace test_rtos {
inline uint32_t tick;
inline uint32_t tickFrequency = 1000;
inline uint32_t wakeLatency;
}

enum osStatus_t { osOK, osError };

using osMemoryPoolId_t = void*;
uint32_t osMemoryPoolGetBlockSize(osMemoryPoolId_t pool);
uint32_t osMemoryPoolGetCapacity(osMemoryPoolId_t pool);
void* osMemoryPoolAlloc(osMemoryPoolId_t pool, uint32_t timeout);
osStatus_t osMemoryPoolFree(osMemoryPoolId_t pool, void* block);

inline uint32_t osKernelGetTickCount() { return test_rtos::tick; }
inline uint32_t osKernelGetTickFreq() { return test_rtos::tickFrequency; }
inline uint32_t osKernelGetSysTimerCount() { return test_rtos::tick; }
inline osStatus_t osDelay(uint32_t ticks) {
    test_rtos::tick += ticks + test_rtos::wakeLatency;
    return osOK;
}

inline osStatus_t osDelayUntil(uint32_t tick) {
    if (static_cast<int32_t>(tick - test_rtos::tick) <= 0) return osError;
    test_rtos::tick = tick + test_rtos::wakeLatency;
    return osOK;
}
