// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#define CG_APPLICATION_BEFORE_NODE_EXECUTION(id) do { \
    if (encodecPlaybackStopping()) { cgStaticError = CG_STOP_SCHEDULER; goto errorHandling; } \
} while (0)

#include "../../common/stream/app_config.hpp"
#include "SdsEndpoint.hpp"
#include "stream_init.hpp"

extern SdsSourceParams encodecReplayParams;
extern SdsSinkParams encodecRecordParams;
void encodecRecordCompletedBlock();

#define CG_BEFORE_FIFO_INIT do { fifos = {}; nodes = {}; } while (0)
#define CG_AFTER_ITERATION encodecRecordCompletedBlock()
