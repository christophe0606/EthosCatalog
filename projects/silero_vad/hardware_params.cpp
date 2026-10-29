// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "hardware_params.h"
#include "embedded_runtime.hpp"
#include "board_nodes.h"
#include "app_sizes.h"
#include "model_pte.h"
#include <cstring>
namespace {
alignas(32) uint8_t method_pool[APP_METHOD_POOL_SIZE] __attribute__((section(APP_POOL_SECTION)));
alignas(32) uint8_t temp_pool[APP_TEMP_POOL_SIZE] __attribute__((section(APP_POOL_SECTION)));
alignas(32) uint16_t frames[2][LCD_PIXELS] __attribute__((section(".bss.lcd_frame_buf")));
int start_audio() { return microphone_start_configured(MIC_SAMPLE_RATE,MIC_BLOCK_SAMPLES); }
int read_audio(int16_t *data) {
    bool gap;
    int error=microphone_read(data,&gap);
    if (gap) {
        ++stream_stats.audio_discontinuities;
        stream_stats.last_discontinuity_block=stream_stats.audio_blocks+1;
    }
    return error;
}
}
extern "C" {
StreamStats stream_stats;
int hardware_params_init(HardwareParams *params) {
    if (!params) return -400;
    auto *module=arm::embedded::initialize_runtime(model_pte,model_pte_size,
        method_pool,sizeof(method_pool),temp_pool,sizeof(temp_pool));
    if (!module) return -401;
    *params={};
    params->module=module; params->stats=&stream_stats;
    params->microphone_start=start_audio; params->microphone_read=read_audio;
    params->microphone_block_samples=MIC_BLOCK_SAMPLES;
    params->framebuffers[0]=frames[0]; params->framebuffers[1]=frames[1];
    params->lcd_present=lcd_present;
    std::memset(frames,0,sizeof(frames));
    return lcd_init_display(frames[0]);
}
void hardware_params_uninit(HardwareParams *params) { if(params) *params={}; }
}
