// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "hardware_params.h"
#include "stream_init.hpp"
#include "cmsis_os2.h"
#include <cstdio>
#include <cstring>

extern "C" int dtln_demo_start(void)
{
    static HardwareParams hardware;
    int error = hardware_params_init(&hardware);
    if (!error) error = stream_configure_and_start(&hardware);
    if (error) return error;
    uint32_t sequence = 0;
    for (;;) {
        osDelay(osKernelGetTickFreq());
        if (stream_stats.error) return stream_stats.error;
        const int32_t previous = osKernelLock();
        const uint32_t observed = stream_stats.stats_sequence;
        DtlnCaseMetrics cases[5];
        std::memcpy(cases, stream_stats.cases, sizeof(cases));
        osKernelRestoreLock(previous);
        if (observed == sequence) continue;
        sequence = observed;
        printf("DTLN demo cycle=%lu blocks=%lu deadline_misses=%lu processing_max_ms=%.3f checks_failed=%lu\n",
            (unsigned long)stream_stats.cycles_completed,
            (unsigned long)stream_stats.output_blocks,
            (unsigned long)stream_stats.deadline_misses,
            1000.0 * stream_stats.processing_ticks_max / osKernelGetSysTimerFreq(),
            (unsigned long)stream_stats.cycle_failures);
        for (unsigned i = 0; i < 5; ++i) {
            printf("DTLN case=%u n=%lu input=%.6f output=%.6f residual=%.6f clean=%.6f\n",
                i, (unsigned long)cases[i].samples, cases[i].input_rms, cases[i].output_rms,
                cases[i].residual_rms, cases[i].clean_rms);
        }
        fflush(stdout);
    }
}
