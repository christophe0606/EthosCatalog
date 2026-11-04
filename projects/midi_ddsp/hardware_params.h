// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// Processing state belongs to the owning node/runtime, never this interface.
typedef struct {
 void *network;
 const float *(*invoke)(void *,unsigned);
 int (*next_window)(void *);
 int (*prepare)(void *,int,const float *);
 int (*controls)(void *,float *);
 const float *(*impulse)(void *);
 int (*audio_write)(const int16_t *,unsigned);
 int (*finished)(void *);
 int (*audio_finish)(void);
} HardwareParams;
int hardware_params_init(HardwareParams *params);
void hardware_params_uninit(HardwareParams *params);
#ifdef __cplusplus
}
#endif
