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
    float *inference_input;
    size_t inference_samples;
    StreamStats *stats;
    int (*microphone_start)(void);
    int (*microphone_read)(int16_t*);
    size_t microphone_block_samples;
    uint16_t *framebuffers[2];
    int (*lcd_touch)(unsigned*, unsigned*, bool*);
    int (*lcd_present)(uint16_t*);
    bool (*ui_touch)(unsigned, unsigned, bool);
    void (*ui_release)(void);
    void (*ui_render)(uint16_t*, const float*, bool);
    size_t model_size, method_pool_size, temp_pool_size;
} HardwareParams;
extern StreamStats stream_stats;
int hardware_params_init(HardwareParams *params);
void hardware_params_uninit(HardwareParams *params);
#ifdef __cplusplus
}
#endif
