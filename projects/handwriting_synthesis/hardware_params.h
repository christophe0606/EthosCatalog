// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "stream_types.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    void *module;
    StreamStats *stats;
    int (*uart_read)(void);
    int (*lcd_init)(uint16_t*);
    int (*lcd_acquire)(void);
    int (*lcd_present)(uint16_t*,unsigned,unsigned,unsigned,unsigned);
    size_t model_size, method_pool_size, temp_pool_size;
} HardwareParams;
extern StreamStats stream_stats;
int hardware_params_init(HardwareParams *params);
void hardware_params_uninit(HardwareParams *params);
#ifdef __cplusplus
}
#endif
