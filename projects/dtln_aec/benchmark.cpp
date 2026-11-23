// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "benchmark.h"
#include "dtln_runtime.h"
#include "model/integer_test_data.h"
#include "model/model_test_data.h"
#include "model_pte.h"
#include "main.h"
#include "cmsis_os2.h"
#include "se_services_port.h"
#include <cmath>
#include <cstdio>
#include <cstring>

extern "C" {
DtlnMethodBenchmark dtln_benchmarks[2];
volatile uint32_t dtln_benchmark_complete, dtln_test_passed;
uint32_t dtln_clock_hz[3];
}

namespace {
float signal_output[512], state_output[DTLN_STATE_VALUES];

bool compare(const float *actual, const float *expected, size_t count, float &maximum)
{
    bool finite = true;
    for (size_t index = 0; index < count; ++index) {
        // Bit tests preserve the check with the Release fast-math flags.
        uint32_t bits;
        std::memcpy(&bits, actual + index, sizeof(bits));
        if ((bits & 0x7f800000U) == 0x7f800000U) {
            finite = false;
            continue;
        }
        const float error = std::fabs(actual[index] - expected[index]);
        if (error > maximum) maximum = error;
    }
    return finite;
}

int measure(unsigned stage)
{
    auto &measurement = dtln_benchmarks[stage - 1];
    const float *signal = stage == 1 ? stage1_input0 : stage2_input0;
    const float *state = stage == 1 ? stage1_input1 : stage2_input1;
    const float *reference = stage == 1 ? stage1_input2 : stage2_input2;
    const float *expected_signal = stage == 1 ? stage1_integer_output0 : stage2_integer_output0;
    const float *expected_state = stage == 1 ? stage1_integer_output1 : stage2_integer_output1;
    const unsigned width = stage == 1 ? 257 : 512;
    const unsigned delegates = stage == 1 ? DTLN_STAGE1_DELEGATES : DTLN_STAGE2_DELEGATES;
    const float signal_tolerance = stage == 1 ? 0.005f : 0.0005f;
    bool outputs_finite = true;
    bool pmu_valid = true;
    // Every iteration uses the same independently supplied warm recurrent state.
    // The two warmups load methods and prime caches before timing samples begin.
    for (unsigned sample = 0; sample < DTLN_BENCHMARK_SAMPLES + 2; ++sample) {
        ethos_benchmark_reset();
        DtlnForwardTiming timing{};
        int error = dtln_forward(stage, signal, state, reference,
            signal_output, state_output, &timing);
        if (error) return error;
        pmu_valid &= ethos_benchmark.invocations == delegates && ethos_benchmark.cycles &&
            !ethos_benchmark.overflow && ethos_benchmark.cycles == ethos_benchmark.active_cycles;
        outputs_finite &= compare(signal_output, expected_signal, width, measurement.max_output_error);
        outputs_finite &= compare(state_output, expected_state, DTLN_STATE_VALUES, measurement.max_state_error);
        if (sample >= 2) {
            const unsigned index = sample - 2;
            measurement.npu_cycles[index] = ethos_benchmark.cycles;
            measurement.adapter_ticks[index] = timing.adapter_ticks;
            measurement.execute_ticks[index] = timing.execute_ticks;
            ++measurement.samples;
        }
    }
    measurement.delegate_count = ethos_benchmark.invocations;
    measurement.last_weights_address = ethos_benchmark.weights_address;
    measurement.last_weights_bytes = ethos_benchmark.weights_size;
    measurement.last_arena_address = ethos_benchmark.arena_address;
    measurement.last_arena_bytes = ethos_benchmark.arena_size;
    size_t method_peak, temp_peak;
    dtln_runtime_memory(&method_peak, &temp_peak);
    measurement.method_pool_peak = method_peak;
    measurement.temp_pool_peak = temp_peak;
    measurement.passed = outputs_finite && pmu_valid &&
        measurement.max_output_error <= signal_tolerance && measurement.max_state_error <= 0.05f;
    printf("METHOD stage%u result=%s output_max=%.9f state_max=%.9f delegates=%lu finite=%u pmu=%u\n",
        stage, measurement.passed ? "PASS" : "FAIL", measurement.max_output_error,
        measurement.max_state_error, (unsigned long)measurement.delegate_count,
        unsigned(outputs_finite), unsigned(pmu_valid));
    printf("MEMORY stage%u model=%lu input=%u method_peak=%lu temp_peak=%lu scope=cumulative_since_boot\n",
        stage, (unsigned long)model_pte_size, unsigned((2 * width + DTLN_STATE_VALUES) * sizeof(float)),
        (unsigned long)measurement.method_pool_peak, (unsigned long)measurement.temp_pool_peak);
    printf("LAST_DELEGATE stage%u weights=%lu@0x%08lx arena=%lu@0x%08lx\n", stage,
        (unsigned long)measurement.last_weights_bytes, (unsigned long)measurement.last_weights_address,
        (unsigned long)measurement.last_arena_bytes, (unsigned long)measurement.last_arena_address);
    for (unsigned sample = 0; sample < DTLN_BENCHMARK_SAMPLES; ++sample)
        printf("SAMPLE stage%u %u npu=%llu adapter=%lu execute=%lu npu_ms=%.6f adapter_ms=%.6f execute_ms=%.6f\n",
            stage, sample, (unsigned long long)measurement.npu_cycles[sample],
            (unsigned long)measurement.adapter_ticks[sample], (unsigned long)measurement.execute_ticks[sample],
            1000.0 * measurement.npu_cycles[sample] / dtln_clock_hz[1],
            1000.0 * measurement.adapter_ticks[sample] / dtln_clock_hz[2],
            1000.0 * measurement.execute_ticks[sample] / dtln_clock_hz[2]);
    fflush(stdout);
    return 0;
}
}

extern "C" int dtln_startup_benchmark(void)
{
    uint32_t service_error = 0;
    if (SERVICES_clocks_setting_get(se_services_s_handle, CLOCK_SETTING_EXTSYS0_FREQ,
            &dtln_clock_hz[0], &service_error) != SERVICES_REQ_SUCCESS || service_error || !dtln_clock_hz[0]) return -411;
    if (SERVICES_clocks_setting_get(se_services_s_handle, CLOCK_SETTING_AXI_FREQ,
            &dtln_clock_hz[1], &service_error) != SERVICES_REQ_SUCCESS || service_error || !dtln_clock_hz[1]) return -411;
    dtln_clock_hz[2] = osKernelGetSysTimerFreq();
    if (!dtln_clock_hz[2]) return -411;
    printf("DTLN clocks CPU=%lu AXI/NPU=%lu timer=%lu OSPI=%lu Hz CRC=%08lx\n",
        (unsigned long)dtln_clock_hz[0], (unsigned long)dtln_clock_hz[1],
        (unsigned long)dtln_clock_hz[2], (unsigned long)dtln_ospi_hz, (unsigned long)dtln_model_crc32);
    printf("DTLN model address=0x%08lx cached_in_sram=%lu\n",
        (unsigned long)dtln_model_address, (unsigned long)dtln_model_in_sram);
    int error = measure(1);
    if (!error) error = measure(2);
    if (error) return error;
    dtln_benchmark_complete = 1;
    dtln_test_passed = dtln_benchmarks[0].passed && dtln_benchmarks[1].passed;
    printf("DTLN_Test_result %s\n", dtln_test_passed ? "PASS" : "FAIL");
    fflush(stdout);
    return dtln_test_passed ? 0 : -413;
}
