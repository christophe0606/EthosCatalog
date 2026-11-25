// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "hardware_params.h"
#include "RTE_Components.h"
#include CMSIS_device_header
#include "peripherals.hpp"

namespace {
alignas(32) uint16_t lcdFrames[2][LCD_PIXELS] __attribute__((section(".bss.lcd_frame_buf")));
DtlnLcd lcd{{lcdFrames[0], lcdFrames[1]}};
DtlnSpeaker speaker;
}

extern "C" {
StreamStats stream_stats;

int hardware_params_init(HardwareParams *params)
{
    if (!params) return -500;
    lcd.status = lcd_init_display(lcd.frames[lcd.front]);
    *params = {&stream_stats, &lcd, nullptr};
    return 0;
}

void hardware_params_uninit(HardwareParams *params)
{
    if (!params) return;
    hardware_params_uninit_speaker(params);
    *params = {};
}

void hardware_params_init_speaker(HardwareParams *params)
{
    if (params->speaker) return;
    speaker.initStatus = devkit_e8_audio_init();
    if (!speaker.initStatus) speaker.initStatus = speaker.setMuted(true);
    params->speaker = &speaker;
}

void hardware_params_uninit_speaker(HardwareParams *params)
{
    if (!params->speaker) return;
    devkit_e8_audio_stop();
    params->speaker = nullptr;
}

void hardware_params_reset_board(void) { NVIC_SystemReset(); }
}
