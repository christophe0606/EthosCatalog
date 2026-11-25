// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "cg_enums.h"
#include "cmsis_os2.h"
#include "sds.h"
#include <cstdint>
#include <type_traits>

// Shared by a replay source and its capture sink on the same graph thread.
struct SdsPlaybackTiming {
    uint32_t timeslot = 0;
    uint32_t nextReleaseTick = 0;
    uint32_t pacingTimerCounts = 0;
    bool started = false;
};

struct SdsSourceParams {
    sdsId_t stream = nullptr;
    SdsPlaybackTiming* timing = nullptr;
    uint32_t sampleRate = 0;
    uint32_t framesPerRecord = 0;  // Sample count per channel.
};

struct SdsSinkParams {
    sdsId_t stream = nullptr;
    const SdsPlaybackTiming* timing = nullptr;
};

namespace sds_node_detail {

template<typename Sample, int samples>
constexpr int32_t recordBytes() {
    static_assert(std::is_trivially_copyable_v<Sample>, "SDS records contain raw sample bytes");
    static_assert(samples > 0 && samples <= INT32_MAX / sizeof(Sample),
                  "SDS record size must fit in a positive int32_t");
    return static_cast<int32_t>(samples * sizeof(Sample));
}

inline int32_t ticksUntil(uint32_t deadline) {
    return static_cast<int32_t>(deadline - osKernelGetTickCount());
}

inline cg_status waitToRetry(int32_t status, int32_t wouldBlock, cg_status bufferError,
                             uint32_t deadline) {
    // SDS 3.1 reports a busy stream lock as SDS_ERROR and has no data-ready event.
    if (status != wouldBlock && status != SDS_ERROR) return CG_BUFFER_ERROR;
    const auto timeout = status == wouldBlock ? bufferError : CG_BUFFER_ERROR;
    if (ticksUntil(deadline) <= 0) return timeout;
    if (osDelay(1) != osOK) return CG_OS_ERROR;
    return ticksUntil(deadline) < 0 ? timeout : CG_SUCCESS;
}

} // namespace sds_node_detail
