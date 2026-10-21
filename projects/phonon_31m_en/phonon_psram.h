// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define PHONON_PSRAM_BASE 0xA0000000UL
#define PHONON_PSRAM_BYTES 0x04000000UL
#ifndef PHONON_PSRAM_HZ
#define PHONON_PSRAM_HZ 25000000UL
#endif
extern volatile uint32_t phonon_psram_verified_bytes;
extern volatile uint32_t phonon_psram_error_address;
extern volatile uint32_t phonon_psram_baud;
int phonon_psram_init_and_test(void);
#ifdef __cplusplus
}
#endif
