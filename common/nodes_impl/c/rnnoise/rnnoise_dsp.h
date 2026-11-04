// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define RN_CONTEXT 1993
#define RN_RESULT 483
void* rn_dsp_create(void);
void rn_dsp_destroy(void*);
void rn_dsp_reset(void*);
void rn_analyze(void*,const int16_t*,float*,float*,uint32_t);
void rn_synthesize(void*,const float*,const float*,float*);
#ifdef __cplusplus
}
#endif
