// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "yamnet_selftest.h"
#include "cmsis_os2.h"
#include "embedded_runtime.hpp"
#include "hardware_params.h"
#include "main.h"
#include "model/model_test_data.h"
#include "nodes_impl/embedded_yamnet.h"
#include "helium/convolution.h"
#include "se_services_port.h"
#include <cmath>
#include <cstdio>
#include <cstring>
extern "C" {
volatile int32_t pipeline_error;
volatile uint32_t yamnet_test_passed, yamnet_benchmark_complete;
volatile uint32_t yamnet_delegates_per_forward;
volatile uint32_t yamnet_fixture_cases_complete;
volatile uint32_t yamnet_ptq_top1_matches, yamnet_float_top1_matches, yamnet_float_top1_in_top5;
volatile uint32_t yamnet_score_parity_passed;
volatile int32_t yamnet_score_parity_error;
volatile uint32_t yamnet_cpu_hz, yamnet_axi_hz, yamnet_timer_hz;
uint64_t yamnet_benchmark_cycles[10];
uint32_t yamnet_benchmark_ticks[10];
float yamnet_backend_error, yamnet_float_error;
float yamnet_test_scores[YAMNET_TEST_CASES][521];
uint64_t yamnet_fixture_cycles[YAMNET_TEST_CASES];
uint32_t yamnet_fixture_ticks[YAMNET_TEST_CASES];
}
namespace {
alignas(32) float scores[521];
alignas(32) float selftest_input[6144];
bool finite_float(float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, 4);
    return (bits & 0x7f800000U) != 0x7f800000U;
}
unsigned argmax(const float *values) {
    unsigned best = 0;
    for (unsigned i = 1; i < 521; ++i)
        if (values[i] > values[best])
            best = i;
    return best;
}
bool in_top5(const float *values, unsigned index) {
    unsigned ahead = 0;
    for (unsigned i = 0; i < 521; ++i)
        if (values[i] > values[index] || (values[i] == values[index] && i < index))
            ++ahead;
    return ahead < 5;
}
} // namespace
extern "C" int yamnet_run_selftest(HardwareParams *params) {
    if (!params || !params->module)
        return pipeline_error = -400;
    HardwareParams &hardware = *params;
    int error = 0;
    uint32_t cpu = 0, axi = 0, se = 0;
    if (SERVICES_clocks_setting_get(se_services_s_handle, CLOCK_SETTING_EXTSYS0_FREQ, &cpu, &se) !=
            SERVICES_REQ_SUCCESS ||
        se || !cpu)
        return pipeline_error = -410;
    if (SERVICES_clocks_setting_get(se_services_s_handle, CLOCK_SETTING_AXI_FREQ, &axi, &se) !=
            SERVICES_REQ_SUCCESS ||
        se || !axi)
        return pipeline_error = -410;
    uint32_t timer = osKernelGetSysTimerFreq();
    if (!timer)
        return pipeline_error = -410;
    yamnet_cpu_hz = cpu;
    yamnet_axi_hz = axi;
    yamnet_timer_hz = timer;
    // Validate every retained pattern, then time ten warmed forwards.
    for (unsigned sample = 0; sample < YAMNET_TEST_CASES + 10; ++sample) {
        unsigned test = sample < YAMNET_TEST_CASES ? sample : (sample - YAMNET_TEST_CASES) % 3;
        std::memcpy(selftest_input, yamnet_test_inputs[test], 6144 * sizeof(float));
        uint32_t ticks = 0;
        yamnet_conv_stats = {};
        ethos_benchmark_reset();
        error = yamnet_forward(hardware.module, selftest_input, scores, &ticks);
        if (error)
            return pipeline_error = error;
        if (yamnet_conv_stats.calls != 14 || yamnet_conv_stats.vector_calls != 14 ||
            yamnet_conv_stats.fallback_calls)
            return pipeline_error = -436;
        if (!yamnet_delegates_per_forward)
            yamnet_delegates_per_forward = ethos_benchmark.invocations;
        if (ethos_benchmark.invocations != 14 || !ethos_benchmark.cycles ||
            ethos_benchmark.overflow || ethos_benchmark.cycles != ethos_benchmark.active_cycles)
            return pipeline_error = -403;
        for (unsigned i = 0; i < 521; ++i) {
            if (!finite_float(scores[i]) || scores[i] < 0 || scores[i] > 1)
                return pipeline_error = -404;
            float e = std::fabs(scores[i] - yamnet_test_quantized[test][i]);
            if (e > yamnet_backend_error)
                yamnet_backend_error = e;
            float f = std::fabs(scores[i] - yamnet_test_float[test][i]);
            if (f > yamnet_float_error)
                yamnet_float_error = f;
            yamnet_test_scores[test][i] = scores[i];
        }
        if (sample < YAMNET_TEST_CASES) {
            unsigned predicted = argmax(scores), original = argmax(yamnet_test_float[test]);
            yamnet_ptq_top1_matches += predicted == argmax(yamnet_test_quantized[test]);
            yamnet_float_top1_matches += predicted == original;
            yamnet_float_top1_in_top5 += in_top5(scores, original);
            yamnet_fixture_cycles[sample] = ethos_benchmark.cycles;
            yamnet_fixture_ticks[sample] = ticks;
            yamnet_fixture_cases_complete = sample + 1;
        } else {
            yamnet_benchmark_cycles[sample - YAMNET_TEST_CASES] = ethos_benchmark.cycles;
            yamnet_benchmark_ticks[sample - YAMNET_TEST_CASES] = ticks;
        }
    }
    printf("YAMNet vector check: %u patches, 521 scores each\n", YAMNET_TEST_CASES);
    printf("Errors: backend=%.9f, original_float=%.9f\n", yamnet_backend_error, yamnet_float_error);
    printf("Classification: PTQ top1=%lu/%u, TensorFlow top1=%lu/%u, TensorFlow top1 in board "
           "top5=%lu/%u\n",
           (unsigned long)yamnet_ptq_top1_matches, YAMNET_TEST_CASES,
           (unsigned long)yamnet_float_top1_matches, YAMNET_TEST_CASES,
           (unsigned long)yamnet_float_top1_in_top5, YAMNET_TEST_CASES);
    printf("Benchmark clocks: CPU=%lu Hz, AXI/NPU=%lu Hz, timer=%lu Hz\n", (unsigned long)cpu,
           (unsigned long)axi, (unsigned long)timer);
    auto usage = arm::embedded::runtime_memory_usage();
    printf("Benchmark memory: model=%lu B, method peak=%lu B, temp peak=%lu B, reserved=%lu B\n",
           (unsigned long)hardware.model_size, (unsigned long)usage.method_peak,
           (unsigned long)usage.temp_peak,
           (unsigned long)(hardware.method_pool_size + hardware.temp_pool_size));
    printf("Benchmark regions: weights=%lu B at 0x%08lx, arena=%lu B at 0x%08lx\n",
           (unsigned long)ethos_benchmark.weights_size,
           (unsigned long)ethos_benchmark.weights_address,
           (unsigned long)ethos_benchmark.arena_size, (unsigned long)ethos_benchmark.arena_address);
    for (unsigned i = 0; i < 10; ++i)
        printf("Benchmark sample %u: NPU=%llu cycles (%.6f ms), forward=%lu ticks (%.6f ms)\n",
               i + 1, (unsigned long long)yamnet_benchmark_cycles[i],
               1000.0 * yamnet_benchmark_cycles[i] / axi, (unsigned long)yamnet_benchmark_ticks[i],
               1000.0 * yamnet_benchmark_ticks[i] / timer);
    printf("Benchmark PMU: invocations=%lu, active=%llu cycles, overflow=%lu\n",
           (unsigned long)ethos_benchmark.invocations,
           (unsigned long long)ethos_benchmark.active_cycles,
           (unsigned long)ethos_benchmark.overflow);
    printf("Helium CPU convolutions: calls=%lu vector=%lu fallback=%lu time_ms=%.6f\n",
           (unsigned long)yamnet_conv_stats.calls, (unsigned long)yamnet_conv_stats.vector_calls,
           (unsigned long)yamnet_conv_stats.fallback_calls, 1000.0 * yamnet_conv_stats.ticks / timer);
    yamnet_benchmark_complete = 1;
    // Classification acceptance and strict score parity are distinct claims.
    yamnet_score_parity_passed = yamnet_backend_error <= 0.005f;
    yamnet_score_parity_error = yamnet_score_parity_passed ? 0 : -405;
    printf("Strict score parity (0.005): %s (%ld)\n", yamnet_score_parity_passed ? "PASS" : "FAIL",
           (long)yamnet_score_parity_error);
    if (yamnet_ptq_top1_matches != YAMNET_TEST_CASES ||
        yamnet_float_top1_matches * 100 < YAMNET_TEST_CASES * 95 ||
        yamnet_float_top1_in_top5 != YAMNET_TEST_CASES) {
        pipeline_error = -406;
        printf("Test_result: FAIL (-406 classification acceptance)\n");
    } else {
        yamnet_test_passed = 1;
        printf("Test_result: PASS (classification acceptance; score parity reported separately)\n");
    }
    fflush(stdout);
    return pipeline_error;
}
