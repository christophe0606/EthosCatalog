// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { DTLN_BENCHMARK_SAMPLES = 10 };
typedef struct {
    uint64_t npu_cycles[DTLN_BENCHMARK_SAMPLES];
    uint32_t adapter_ticks[DTLN_BENCHMARK_SAMPLES];
    uint32_t execute_ticks[DTLN_BENCHMARK_SAMPLES];
    uint32_t method_pool_peak, temp_pool_peak;
    uint32_t last_weights_address, last_weights_bytes, last_arena_address, last_arena_bytes;
    uint32_t passed, samples, delegate_count;
    float max_output_error, max_state_error;
} DtlnMethodBenchmark;

extern DtlnMethodBenchmark dtln_benchmarks[2];
extern volatile uint32_t dtln_benchmark_complete, dtln_test_passed;
extern uint32_t dtln_clock_hz[3];
int dtln_startup_benchmark(void);

#ifdef __cplusplus
}
#endif
