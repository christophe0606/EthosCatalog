// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
/*

Generated with CMSIS-Stream python scripts.
The generated code is not covered by CMSIS-Stream license.

The support classes and code are covered by CMSIS-Stream license.

*/

#ifndef SCHEDULER_APP_H_
#define SCHEDULER_APP_H_


#include <stdint.h>

#ifdef   __cplusplus
extern "C"
{
#endif

#include "cstream_node.h"


/* Node identifiers */
#define STREAM_APP_NB_IDENTIFIED_NODES 4
#define STREAM_APP_AUDIO_WINDOW_ID 0
#define STREAM_APP_SEND_ID 1
#define STREAM_APP_LCD_ID 2
#define STREAM_APP_NETWORK_ID 3

#define STREAM_APP_SCHED_LEN 5

/* Selectors global identifiers */
#define SEL_REQUEST_ID 100
#define SEL_COMPLETE_ID 101
#define SEL_RESULT_ID 102

extern CStreamNode* get_scheduler_app_node(int32_t nodeID);

extern int init_scheduler_app(void *evtQueue_,AppParams *params);
extern void free_scheduler_app();
extern uint32_t scheduler_app(int *error);
extern void reset_fifos_scheduler_app(int all);

#ifdef   __cplusplus
}
#endif

#endif

