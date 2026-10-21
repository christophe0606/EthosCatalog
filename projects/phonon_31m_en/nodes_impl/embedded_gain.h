// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
// Keep ExecuTorch's global platform enums out of Stream translation units.
int embedded_gain_forward(void *module, float *input, float *output, size_t count);
#ifdef __cplusplus
}
#endif
