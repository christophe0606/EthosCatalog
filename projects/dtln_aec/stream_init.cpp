// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "dtln_app_config.hpp"
#include "IdentifiedNode.hpp"
#include "scheduler_app.h"
#include "stream_init.hpp"

int stream_run(const HardwareParams& hardware)
{
    app_params_set_hardware(&hardware);
    int error = init_scheduler_app(nullptr, &appParams);
    if (error != CG_SUCCESS) return error;
    reset_fifos_scheduler_app(1);
    scheduler_app(&error);
    free_scheduler_app();
    return error;
}
