// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "hardware_params.h"

extern "C" {
StreamStats stream_stats;
int hardware_params_init(HardwareParams *params)
{
    if (!params) return -500;
    *params = {&stream_stats, dtln_forward, nullptr, nullptr};
    return 0;
}
void hardware_params_uninit(HardwareParams *params)
{
    if (params) *params = {};
}
}
