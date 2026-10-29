// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "hardware_params.h"
#ifdef __cplusplus
extern "C" {
#endif
// Run once with an initialized runtime. Retains classification, score-parity,
// PMU, timing and allocator evidence in the yamnet_* symbols/UART report.
int yamnet_run_selftest(HardwareParams *params);
#ifdef __cplusplus
}
#endif
