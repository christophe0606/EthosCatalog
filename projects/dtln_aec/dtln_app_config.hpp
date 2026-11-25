// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#define CG_APPLICATION_BEFORE_NODE_EXECUTION(id) do { \
    if (dtlnPlaybackStopping()) { cgStaticError = CG_STOP_SCHEDULER; goto errorHandling; } \
} while (0)

#include "../../common/stream/app_config.hpp"
#include "SdsEndpoint.hpp"
#include "stream_init.hpp"

extern SdsSourceParams dtlnReplayParams;
extern SdsSinkParams dtlnRecordParams;
void dtlnRecordHopTicks(uint32_t ticks);

// Let the generated free function clean up a partially initialized graph.
#define CG_BEFORE_FIFO_INIT do { fifos = {}; nodes = {}; } while (0)
#define CG_BEFORE_ITERATION const uint32_t hopStartTicks = osKernelGetSysTimerCount()
#define CG_AFTER_ITERATION dtlnRecordHopTicks(osKernelGetSysTimerCount() - hopStartTicks)
