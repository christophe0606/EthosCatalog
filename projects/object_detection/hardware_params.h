// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "stream_types.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    void *module;
    int8_t *inference_input;
    uint16_t *camera_buffer;
    uint16_t *framebuffers[2];
    StreamStats *stats;
} HardwareParams;
int hardware_params_init(HardwareParams *params);
void hardware_params_uninit(HardwareParams *params);
#ifdef __cplusplus
}
#endif
