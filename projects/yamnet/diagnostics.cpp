// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "diagnostics.h"
#include "board_nodes.h"
#include "cmsis_os2.h"
#include "embedded_runtime.hpp"
#include <cstdio>
extern "C" volatile uint32_t yamnet_timer_hz;
void yamnet_print_health(const StreamStats *s) {
    auto memory = arm::embedded::runtime_memory_usage();
    printf("YAMNet live: audio=%lu features=%lu infer=%lu lcd=%lu skipped=%lu deadline=%lu "
           "capture_drops=%lu overflow=%lu requests_error=%lu pipeline=%ld forward_ms=%.3f "
           "npu_ms=%.3f stack_free_audio=%lu stack_free_infer=%lu method_peak=%lu temp_peak=%lu\n",
           (unsigned long)s->audio_blocks, (unsigned long)s->feature_frames,
           (unsigned long)s->inferences, (unsigned long)s->frames_presented,
           (unsigned long)s->coalesced_windows, (unsigned long)s->audio_deadline_misses,
           (unsigned long)microphone_drops, (unsigned long)microphone_overflows,
           (unsigned long)s->request_errors, (long)s->error,
           1000.0 * s->forward_ticks / yamnet_timer_hz, 1000.0 * s->npu_cycles / 400000000.0,
           (unsigned long)s->audio_stack_free, (unsigned long)s->inference_stack_free,
           (unsigned long)memory.method_peak, (unsigned long)memory.temp_peak);
    fflush(stdout);
}
