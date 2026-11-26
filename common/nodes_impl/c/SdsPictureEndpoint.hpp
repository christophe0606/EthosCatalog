// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "SdsEndpoint.hpp"

enum class SdsPictureOverflowPolicy { Fail, Skip };

struct SdsPictureSourceParams {
    // The application owns the pool and keeps it alive until all BufferPtrs are released.
    static constexpr uint32_t poolBlockBytes(uint32_t pictureBytes) {
        return pictureBytes + sizeof(osMemoryPoolId_t);
    }

    sdsId_t stream = nullptr;
    osMemoryPoolId_t pool = nullptr;
    uint32_t pictureBytes = 0;
    uint32_t timeoutTicks = 0;
    uint32_t framesPerSecond = 0;  // Zero checks timestamp order without enforcing an interval.
    SdsPictureOverflowPolicy overflowPolicy = SdsPictureOverflowPolicy::Fail;
};

struct SdsPictureSinkParams {
    sdsId_t stream = nullptr;
    uint32_t pictureBytes = 0;
    uint32_t timeoutTicks = 0;
};
