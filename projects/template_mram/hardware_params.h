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
    StreamStats *stats;
    int (*microphone_start)(void);
    int (*microphone_read)(int16_t*, bool*);
    uint16_t *framebuffers[2];
    float *lcd_magnitude;
    volatile uint32_t *lcd_peak_hz;
    int (*lcd_touch)(unsigned*, unsigned*, bool*);
    int (*lcd_present)(uint16_t*);
    bool (*ui_touch)(unsigned, unsigned, bool);
    void (*ui_release)(void);
    void (*ui_render)(uint16_t*, bool);
    size_t model_size, method_pool_size, temp_pool_size;
} HardwareParams;
extern StreamStats stream_stats;
int hardware_params_init(HardwareParams *params);
void hardware_params_uninit(HardwareParams *params);
#ifdef __cplusplus
}
#endif
