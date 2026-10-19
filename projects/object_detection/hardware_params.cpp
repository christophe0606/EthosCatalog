// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "hardware_params.h"
#include "board_nodes.h"
#include <cstring>
namespace {
alignas(32) uint16_t camera_buffer[BOARD_CAMERA_BYTES/2] __attribute__((section(".bss.camera_frame_buf")));
alignas(32) uint16_t framebuffers[2][LCD_PIXELS] __attribute__((section(".bss.lcd_frame_buf")));
}
extern "C" {
StreamStats stream_stats;
int hardware_params_init(HardwareParams *params)
{
    if (!params) return -800;
    extern void *detection_module(void);
    extern int8_t *detection_input(void);
    *params = {};
    params->module = detection_module(); params->inference_input = detection_input();
    params->camera_buffer = camera_buffer; params->stats = &stream_stats;
    params->framebuffers[0] = framebuffers[0]; params->framebuffers[1] = framebuffers[1];
    std::memset(framebuffers, 0, sizeof(framebuffers));
    // Touch and camera share I2C1; this demo does not use touch. Initializing
    // GT911 would retain its callback and prevent the sensor's completion flag.
    int error = lcd_init_display(framebuffers[0]);
    if (!error) error = camera_init();
    return error;
}
void hardware_params_uninit(HardwareParams *params) { if (params) *params = {}; }
}
