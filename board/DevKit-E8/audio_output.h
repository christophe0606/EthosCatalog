// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { DEVKIT_E8_AUDIO_RATE = 16000, DEVKIT_E8_AUDIO_FRAMES = 256,
       DEVKIT_E8_AUDIO_SLOTS = 6 };
typedef struct {
    uint32_t queued_blocks, completed_blocks, callbacks, silence_blocks;
    uint32_t starvation_events, driver_underflows, deadline_misses;
    uint32_t queue_high_water, write_timeouts, frame_errors;
    uint32_t interval_min_ticks, interval_max_ticks, timer_hz;
    uint32_t started_ticks, first_audio_ticks, last_completed_ticks;
    uint32_t codec_left_volume_code, codec_right_volume_code;
    int32_t error;
} DevKitE8AudioStats;
// Inspect while halted; fields are updated by the producer or SAI interrupt.
extern volatile DevKitE8AudioStats devkit_e8_audio_stats;
int devkit_e8_audio_init(void);
// One producer thread. Copies exactly 256 mono PCM16 samples to a bounded
// stereo I2S queue, waiting for space. Starts after two blocks (32 ms).
int devkit_e8_audio_write(const int16_t *mono, unsigned frames);
// Flushes a partial startup queue, waits for all application blocks and the
// short hardware FIFO to drain. Does not stop the continuous silence clock.
int devkit_e8_audio_drain(unsigned timeout_ms);
// Call drain first when the queued audio should finish; stop otherwise aborts.
void devkit_e8_audio_stop(void);
#ifdef __cplusplus
}
#endif
