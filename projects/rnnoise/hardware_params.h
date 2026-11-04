// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "stream_types.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {void* module;StreamStats* stats;int(*microphone_start)(void);int(*microphone_read)(int16_t*);size_t microphone_block_samples;uint16_t* framebuffers[2];int(*lcd_present)(uint16_t*);uint32_t(*audio_epoch)(void);void(*analysis_observed)(uint32_t);void(*synthesis_observed)(uint32_t);} HardwareParams;
extern StreamStats stream_stats;
int hardware_params_init(HardwareParams*);
void hardware_params_uninit(HardwareParams*);
#ifdef __cplusplus
}
#endif
