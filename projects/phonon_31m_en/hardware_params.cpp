// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
// Reserved CMSIS Stream adapter. The measured diagnostic uses app_main.cpp.
#include "hardware_params.h"
extern "C" {
StreamStats stream_stats{};
int hardware_params_init(HardwareParams *params)
{
    if (!params) return -400;
    *params = {};
    // UART-to-audio application construction is deferred after feasibility FAIL.
    return -540;
}
void hardware_params_uninit(HardwareParams *params)
{
    if (params) *params = {};
}
}
