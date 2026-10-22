// SPDX-License-Identifier: Apache-2.0
#pragma once
// DevKit-E8 camera, microphone and LCD configuration and driver interface.
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define MICROPHONE_BLOCK_SAMPLES 512U
#define LCD_PIXELS (480U * 800U)
// Optional UART4 interrupt ring, enabled by BOARD_UART_ASYNC_RX.
int board_uart_rx_start(void);
int board_uart_rx_read(void); // -1 when empty; one consumer
extern volatile uint32_t board_uart_rx_drops, board_uart_rx_errors;
#define BOARD_CAMERA_WIDTH 640U
#define BOARD_CAMERA_HEIGHT 480U
#define BOARD_CAMERA_BYTES (BOARD_CAMERA_WIDTH * BOARD_CAMERA_HEIGHT * 2U)
// Single-shot RGB565. The caller retains the buffer until the next capture.
int camera_init(void);
int camera_capture(uint16_t *frame, uint32_t timeout_ticks);
extern volatile uint32_t camera_frames, camera_errors, camera_timeouts;
// Preserve every error notification, including before a capture is requested.
enum { CAMERA_INITIALIZING, CAMERA_IDLE, CAMERA_CAPTURING, CAMERA_STOPPING };
typedef struct { uint32_t phase, error_count[4], error_mask[4]; } CameraDiagnostics;
extern volatile CameraDiagnostics camera_diagnostics;
// One caller, the audio thread. Blocks without polling; reports sequence gaps.
int microphone_start(void);
// Configure mono capture before the first read. Supports 16/48 kHz, up to 512 samples.
int microphone_start_configured(uint32_t sample_rate, uint32_t block_samples);
int microphone_read(int16_t *block, bool *discontinuity);
extern volatile uint32_t microphone_drops, microphone_overflows;
// Initialize before starting audio. Only the LCD thread calls present/touch.
int lcd_init(uint16_t *initial_frame);
// Display-only initialization leaves I2C1 available to the camera. lcd_init
// initializes GT911 and retains that bus callback; the two cannot own it together.
int lcd_init_display(uint16_t *initial_frame);
int lcd_present(uint16_t *frame);
// Optional split presentation, one owning thread. Before mutating the previous
// front buffer, wait for release; submit the new immutable front buffer, then
// do independent work while two scanline events release the previous buffer.
// Do not mix the split API with another presentation caller.
int lcd_wait_for_release(void);
int lcd_submit(uint16_t *frame);
// Native RGB565 rectangle (480x800). Only this rectangle may have changed since
// the buffer was last cleaned. Cache-line rounding stays inside the framebuffer.
// Zero-sized damage submits an unchanged buffer. Same release contract as submit.
int lcd_submit_region(uint16_t *frame,unsigned x,unsigned y,unsigned width,unsigned height);
int lcd_touch(unsigned *x, unsigned *y, bool *contact);
#ifdef __cplusplus
}
#endif
