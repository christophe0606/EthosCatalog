// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "dtln_runtime.h"
#include "benchmark.h"

extern "C" {
volatile int32_t pipeline_error;
// The application supplies its Stream startup after the independent benchmarks.
__attribute__((weak)) int dtln_demo_start(void) { return 0; }

int app_main(void)
{
    int error = dtln_runtime_init();
    if (!error) error = dtln_startup_benchmark();
    if (!error) error = dtln_demo_start();
    pipeline_error = error;
    return error;
}
}
