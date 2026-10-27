// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdint.h>
#include "stream_types.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    StreamStats *stats;
    int (*uart_read)(void);
    int (*audio_write)(const int16_t*,unsigned);
    int (*lcd_init)(uint16_t*);
    int (*lcd_acquire)(void);
    int (*lcd_present)(uint16_t*,unsigned,unsigned,unsigned,unsigned);
} HardwareParams;
extern StreamStats stream_stats;
int hardware_params_init(HardwareParams*);
#ifdef __cplusplus
}
#endif
