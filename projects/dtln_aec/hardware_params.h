// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "stream_types.h"

#ifdef __cplusplus
extern "C" {
#endif
typedef struct DtlnLcd DtlnLcd;
typedef struct DtlnSpeaker DtlnSpeaker;

typedef struct {
    StreamStats *stats;
    DtlnLcd *lcd;
    DtlnSpeaker *speaker;
} HardwareParams;
extern StreamStats stream_stats;
int hardware_params_init(HardwareParams *params);
void hardware_params_uninit(HardwareParams *params);
void hardware_params_init_speaker(HardwareParams *params);
void hardware_params_uninit_speaker(HardwareParams *params);
void hardware_params_reset_board(void);
#ifdef __cplusplus
}
#endif
