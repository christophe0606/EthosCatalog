// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "hardware_params.h"

#include "RTE_Components.h"
#include CMSIS_device_header
#include "board_nodes.h"
#include "nodes_impl/waveform.hpp"
#include "se_services_port.h"

#include <cstring>

namespace {
using namespace encodec::waveform;
alignas(32) uint16_t lcdFrames[2][width * height] __attribute__((section(".bss.lcd_frame_buf")));
static_assert(sizeof(lcdFrames[0]) == 204800);
EnCodecLcd lcd{{lcdFrames[0], lcdFrames[1]}, 0, 0};
}

extern "C" {
EnCodecDemoStats encodecDemoStats;

void hardware_params_init(HardwareParams* params)
{
    // The frame buffers are in an uninitialized linker region.
    std::memset(lcdFrames[0], 0, sizeof(lcdFrames[0]));
    lcd.status = lcd_init_display_window(lcdFrames[0], 176, encodec::waveform::height, SRAM1_MASK);
    encodecDemoStats.lcd_error = lcd.status;
    *params = {&lcd, &encodecDemoStats};
}

void hardware_params_reset_board(void)
{
    NVIC_SystemReset();
}
}
