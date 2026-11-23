// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "../../common/stream/app_config.hpp"
#include "SdsEndpoint.hpp"

extern SdsSourceParams dtlnReplayParams;
extern SdsSinkParams dtlnRecordParams;
bool dtlnPlaybackStopping();
void dtlnRecordHopTicks(uint32_t ticks);

#undef CG_BEFORE_NODE_EXECUTION
#define CG_BEFORE_NODE_EXECUTION(id) do { \
    if (dtlnPlaybackStopping()) { cgStaticError = CG_STOP_SCHEDULER; goto errorHandling; } \
} while (0);

#define CG_BEFORE_ITERATION const uint32_t hopStartTicks = osKernelGetSysTimerCount()
#define CG_AFTER_ITERATION dtlnRecordHopTicks(osKernelGetSysTimerCount() - hopStartTicks)
