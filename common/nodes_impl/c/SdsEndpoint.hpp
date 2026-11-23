// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "cg_enums.h"
#include "sds.h"
#include <cstdint>
#include <type_traits>

struct SdsRetry {
    // Yield or wait before another attempt. Return false to stop the scheduler.
    bool (*wait)(void* context, int32_t status) = nullptr;
    void* context = nullptr;
    uint32_t maxRetries = 0;

    bool valid() const { return maxRetries == 0 || wait != nullptr; }
};

struct SdsRecordObserver {
    // Called once after a complete transfer. The record is valid only for the call.
    void (*observe)(void* context, const void* record, uint32_t bytes) = nullptr;
    void* context = nullptr;
};

struct SdsSourceParams {
    sdsId_t stream = nullptr;
    uint32_t* timeslot = nullptr;  // Updated on successful playback only.
    SdsRetry retry{};
    SdsRecordObserver observer{};
    // Allows playback to wait for the record's timeslot before publishing it.
    cg_status (*beforePublish)(uint32_t timeslot) = nullptr;
};

struct SdsSinkParams {
    sdsId_t stream = nullptr;
    uint32_t (*timeslot)(void* context) = nullptr;
    void* context = nullptr;
    SdsRetry retry{};
    SdsRecordObserver observer{};
};

namespace sds_node_detail {

template<typename Sample, int samples>
constexpr int32_t recordBytes() {
    static_assert(std::is_trivially_copyable_v<Sample>, "SDS records contain raw sample bytes");
    static_assert(samples > 0 && samples <= INT32_MAX / sizeof(Sample),
                  "SDS record size must fit in a positive int32_t");
    return static_cast<int32_t>(samples * sizeof(Sample));
}

template<typename Transfer>
cg_status transferRecord(const SdsRetry& retry, Transfer transfer, int32_t expectedBytes,
                         int32_t wouldBlock, cg_status bufferError, int32_t& status) {
    for (uint32_t remaining = retry.maxRetries;; --remaining) {
        status = transfer();
        if (status == expectedBytes) return CG_SUCCESS;
        if (status == SDS_EOS && wouldBlock == SDS_NO_DATA) return CG_STOP_SCHEDULER;

        // SDS 3.1 can also return SDS_ERROR if its worker holds the stream lock.
        if (status != wouldBlock && status != SDS_ERROR) return CG_BUFFER_ERROR;
        if (remaining == 0) return status == wouldBlock ? bufferError : CG_BUFFER_ERROR;
        if (!retry.wait(retry.context, status)) return CG_STOP_SCHEDULER;
    }
}

} // namespace sds_node_detail
