// SPDX-License-Identifier: Apache-2.0
#pragma once
extern "C" {
#include "RTE_Components.h"
#include CMSIS_device_header
#include "cmsis_os2.h"
#include "datatypes.h"
#include "app_params.h"
}
#include "cg_enums.h"
#include "stream_rtos_events.h"
extern osEventFlagsId_t cg_streamEvent;
#ifndef CG_BEFORE_BUFFER
#define CG_BEFORE_BUFFER __ALIGNED(16) __attribute__((section(".bss.stream_fifo")))
#endif

#ifndef CG_APPLICATION_BEFORE_NODE_EXECUTION
#define CG_APPLICATION_BEFORE_NODE_EXECUTION(id)
#endif

#define CG_BEFORE_NODE_EXECUTION(id) do { \
    uint32_t flags = osEventFlagsWait(cg_streamEvent, STREAM_PAUSE_EVENT | STREAM_DONE_EVENT, osFlagsWaitAny, 0); \
    if (!(flags & osFlagsError)) { \
        if (flags & STREAM_DONE_EVENT) { cgStaticError = CG_STOP_SCHEDULER; goto errorHandling; } \
        if (flags & STREAM_PAUSE_EVENT) { cgStaticError = CG_PAUSED_SCHEDULER; goto errorHandling; } \
    } \
    CG_APPLICATION_BEFORE_NODE_EXECUTION(id); \
} while (0);
