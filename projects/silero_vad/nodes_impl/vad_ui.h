// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void vad_render(uint16_t *frame,unsigned alpha);
#ifdef __cplusplus
}
#endif
