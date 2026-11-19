// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { DTLN_STATE_VALUES = 512, DTLN_FRAME_SAMPLES = 512, DTLN_HOP_SAMPLES = 128 };
typedef struct {
    uint32_t adapter_ticks;
    uint32_t execute_ticks;
} DtlnForwardTiming;

// Initialize once after app_prepare. Calls are serialized by the application.
int dtln_runtime_init(void);
// Stage is 1 (257 magnitudes/mask) or 2 (512 time samples).
// Inputs are copied to owned SRAM0 storage. Output/state may alias their inputs.
int dtln_forward(unsigned stage, const float *signal, const float *state,
                 const float *reference, float *output, float *next_state,
                 DtlnForwardTiming *timing);
void dtln_runtime_memory(size_t *method_peak, size_t *temp_peak);
extern volatile uint32_t dtln_model_verified, dtln_model_crc32;
extern volatile uint32_t dtln_ospi_hz, dtln_axi_hz;
extern volatile uint32_t dtln_model_address, dtln_model_in_sram;
extern volatile int32_t dtln_runtime_error;

#ifdef __cplusplus
}
#endif
