// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "arm_math.h"
#ifdef __cplusplus
extern "C" {
#endif
// Caller owns all buffers. Each call is one processing stage; no RTOS inside.
typedef struct { float *data; size_t size, valid; } sliding_buffer_t;
void sliding_buffer_init(sliding_buffer_t *state, float *storage, size_t size);
bool sliding_buffer_push(sliding_buffer_t *state, const float *block, size_t count);
void pcm_to_float(const int16_t *pcm, float *out, size_t count);
void remove_dc(const float *in, float *out, size_t count);
float hann_window_init(float *window, size_t count);
void apply_window(const float *in, const float *window, float *out, size_t count);
int fft_2048_init(arm_rfft_fast_instance_f32 *fft);
void fft_forward(arm_rfft_fast_instance_f32 *fft, float *in, float *packed);
void fft_magnitude(const float *packed, float *out, size_t fft_size, float scale);
#ifdef __cplusplus
}
#endif
