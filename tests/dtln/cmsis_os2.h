// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "../rtos/cmsis_os2.h"
#include <chrono>
#include <condition_variable>
#include <mutex>

class stream_event_flags;
using osEventFlagsId_t = stream_event_flags;
inline constexpr uint32_t osFlagsWaitAny = 0;
inline constexpr uint32_t osFlagsError = 0x80000000U;

uint32_t osEventFlagsWait(osEventFlagsId_t& event, uint32_t mask, uint32_t options,
                          uint32_t timeout);
inline osStatus_t osEventFlagsDelete(void*) { return osOK; }

namespace test_rtos {
struct Semaphore {
    std::mutex mutex;
    std::condition_variable ready;
    bool signaled = false;
};
}

using osSemaphoreId_t = test_rtos::Semaphore*;

inline osSemaphoreId_t osSemaphoreNew(uint32_t, uint32_t initial, const void*) {
    auto* semaphore = new test_rtos::Semaphore;
    semaphore->signaled = initial != 0;
    return semaphore;
}

inline osStatus_t osSemaphoreRelease(osSemaphoreId_t semaphore) {
    {
        std::lock_guard guard(semaphore->mutex);
        semaphore->signaled = true;
    }
    semaphore->ready.notify_one();
    return osOK;
}

inline osStatus_t osSemaphoreAcquire(osSemaphoreId_t semaphore, uint32_t timeout) {
    std::unique_lock lock(semaphore->mutex);
    if (!semaphore->ready.wait_for(lock, std::chrono::milliseconds(timeout),
                                   [&] { return semaphore->signaled; })) {
        return osError;
    }
    semaphore->signaled = false;
    return osOK;
}

inline osStatus_t osSemaphoreDelete(osSemaphoreId_t semaphore) {
    delete semaphore;
    return osOK;
}
