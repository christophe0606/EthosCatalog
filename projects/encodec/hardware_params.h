// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "stream_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t* frames[2];
    unsigned front;
    int status;
} EnCodecLcd;

typedef struct {
    EnCodecLcd* lcd;
    EnCodecDemoStats* stats;
} HardwareParams;

extern EnCodecDemoStats encodecDemoStats;
void hardware_params_init(HardwareParams* params);
void hardware_params_reset_board(void);

#ifdef __cplusplus
}
#endif
