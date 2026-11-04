// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "hardware_params.h"
#include "stream_init.hpp"
extern "C" int rnnoise_startup_test(void*);
extern "C" volatile int32_t pipeline_error;
static HardwareParams hardware;
extern "C" int app_main(){int e=hardware_params_init(&hardware);if(!e)e=rnnoise_startup_test(hardware.module);if(!e)e=stream_configure_and_start(&hardware);if(e){pipeline_error=e;stream_stats.error=e;}return e;}
