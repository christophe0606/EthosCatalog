// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* One context per independent utterance. The node owns this bounded storage. */
typedef union { uint64_t alignment; unsigned char bytes[8192]; } RvttsFarganCpuState;
#if defined(RVTTS_FARGAN_HOST) && defined(_WIN32)
#define RVTTS_FARGAN_API __declspec(dllexport)
#else
#define RVTTS_FARGAN_API
#endif
RVTTS_FARGAN_API int rvtts_fargan_cpu_bind(const void *weights, size_t size);
RVTTS_FARGAN_API void rvtts_fargan_cpu_prime(RvttsFarganCpuState *state, const float features[20]);
RVTTS_FARGAN_API void rvtts_fargan_cpu_frame(RvttsFarganCpuState *state, const float features[20], float pcm[160]);
RVTTS_FARGAN_API void rvtts_fargan_cpu_state(const RvttsFarganCpuState *state, float out[964]);
extern volatile uint32_t fargan_cpu_verified, fargan_cpu_crc32, fargan_cpu_kernel_mve;
extern volatile uint32_t fargan_cpu_kernel_rows;
extern volatile uint32_t fargan_cpu_kernel_packed,fargan_cpu_dtcm_bytes;
#ifdef __cplusplus
}
#endif
