// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdint.h>
typedef struct { unsigned length; } arm_rfft_fast_instance_f32;
typedef int arm_status;
enum { ARM_MATH_SUCCESS = 0 };
#define DTLN_TEST_INIT(N) inline arm_status arm_rfft_fast_init_##N##_f32(arm_rfft_fast_instance_f32* instance) { instance->length = N; return ARM_MATH_SUCCESS; }
DTLN_TEST_INIT(32)
DTLN_TEST_INIT(64)
DTLN_TEST_INIT(128)
DTLN_TEST_INIT(256)
DTLN_TEST_INIT(1024)
DTLN_TEST_INIT(4096)
#undef DTLN_TEST_INIT
inline arm_status arm_rfft_fast_init_512_f32(arm_rfft_fast_instance_f32* instance) {
    instance->length = 512;
    return ARM_MATH_SUCCESS;
}
inline arm_status arm_rfft_fast_init_2048_f32(arm_rfft_fast_instance_f32* instance) {
    instance->length = 2048;
    return ARM_MATH_SUCCESS;
}
void arm_rfft_fast_f32(const arm_rfft_fast_instance_f32*, float*, float*, uint8_t);
void arm_cmplx_mag_f32(const float*, float*, uint32_t);
void arm_scale_f32(const float*, float, float*, uint32_t);
