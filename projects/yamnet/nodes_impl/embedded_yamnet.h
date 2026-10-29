// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdint.h>
int yamnet_forward(void *module, float *features, float *scores, uint32_t *ticks = nullptr);
