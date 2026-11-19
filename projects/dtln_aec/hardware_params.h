// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "dtln_runtime.h"
#include "stream_types.h"

#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    StreamStats *stats;
    int (*forward)(unsigned, const float*, const float*, const float*,
                   float*, float*, DtlnForwardTiming*);
} HardwareParams;
extern StreamStats stream_stats;
int hardware_params_init(HardwareParams *params);
void hardware_params_uninit(HardwareParams *params);
#ifdef __cplusplus
}
#endif
