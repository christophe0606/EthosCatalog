// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdint.h>
// Float inputs are CPU boundaries; lowered NPU buffers use the SRAM allocator.
// h/c input may alias next_h/next_c output;
// the adapter copies outputs only after forward has completed.
int silero_forward(void *module,float *audio,float *h,float *c,
                   float *probability,float *next_h,float *next_c,uint32_t *ticks=nullptr);
