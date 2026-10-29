// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "frontend.h"
#include "hardware_params.h"
#include "stream_init.hpp"
#include "yamnet_selftest.h"
extern "C" volatile int32_t pipeline_error;
extern "C" {
StreamStats stream_stats;
}
extern "C" int app_main(void) {
    static HardwareParams hardware;
    int error = hardware_params_init(&hardware);
    if (!error)
        error = yamnet_frontend_selftest();
    if (!error)
        error = yamnet_run_selftest(&hardware);
    if (!error)
        error = stream_configure_and_start(&hardware);
    if (error) {
        pipeline_error = error;
        stream_stats.error = error;
    }
    return error;
}
