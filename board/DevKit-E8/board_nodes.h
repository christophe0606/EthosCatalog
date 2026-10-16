// SPDX-License-Identifier: Apache-2.0
#pragma once
// DevKit-E8 microphone and LCD configuration and driver interface.
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define MICROPHONE_BLOCK_SAMPLES 512U
#define LCD_PIXELS (480U * 800U)
// One caller, the audio thread. Blocks without polling; reports sequence gaps.
int microphone_start(void);
int microphone_read(int16_t *block, bool *discontinuity);
extern volatile uint32_t microphone_drops, microphone_overflows;
// Initialize before starting audio. Only the LCD thread calls present/touch.
int lcd_init(uint16_t *initial_frame);
int lcd_present(uint16_t *frame);
int lcd_touch(unsigned *x, unsigned *y, bool *contact);
#ifdef __cplusplus
}
#endif
