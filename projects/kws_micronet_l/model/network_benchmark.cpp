// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
// Network-only validation and timing. No audio, LCD or CMSIS Stream execution.
#include <array>
#include <cstdio>
#include <cstring>
#include "embedded_runtime.hpp"
#include "model/model_test_data.h"
#include "model_pte.h"
#include "cmsis_os2.h"
#include "main.h"
#include "se_services_port.h"

extern "C" {
volatile int32_t pipeline_error;
volatile uint32_t kws_test_passed, kws_benchmark_complete, kws_progress, kws_heartbeat;
uint64_t kws_cycles[KWS_BENCHMARK_SAMPLES];
uint32_t kws_forward_ticks[KWS_BENCHMARK_SAMPLES];
int8_t kws_outputs[KWS_TEST_CASES][KWS_OUTPUT_ELEMENTS];
struct KwsResults {
    uint32_t cases, max_default_error, max_reference_error, max_pytorch_error;
    uint32_t default_top1_mismatches, reference_top1_mismatches, pytorch_top1_mismatches;
    uint32_t cpu_hz, npu_hz, timer_hz, method_peak, temp_peak;
} kws_results;
}
namespace {
alignas(32) uint8_t method_pool[APP_METHOD_POOL_SIZE] __attribute__((section(APP_POOL_SECTION)));
alignas(32) uint8_t temp_pool[APP_TEMP_POOL_SIZE] __attribute__((section(APP_POOL_SECTION)));
alignas(32) int8_t inference_input[KWS_INPUT_ELEMENTS] __attribute__((section(APP_POOL_SECTION)));
arm::embedded::EmbeddedModule *module;

unsigned top1(const int8_t *values)
{
    unsigned best = 0;
    for (unsigned i = 1; i < KWS_OUTPUT_ELEMENTS; ++i)
        if (values[i] > values[best]) best = i;
    return best;
}
unsigned max_error(const int8_t *actual, const int8_t *expected)
{
    unsigned maximum = 0;
    for (unsigned i = 0; i < KWS_OUTPUT_ELEMENTS; ++i) {
        int delta = int(actual[i]) - int(expected[i]);
        unsigned error = delta < 0 ? unsigned(-delta) : unsigned(delta);
        if (error > maximum) maximum = error;
    }
    return maximum;
}
int forward(int8_t *output, uint32_t *ticks)
{
    using namespace executorch::aten;
    std::array<SizesType, 4> sizes{1, 49, 10, 1};
    std::array<DimOrderType, 4> order{0, 1, 2, 3};
    TensorImpl impl(ScalarType::Char, sizes.size(), sizes.data(), inference_input, order.data());
    Tensor tensor(&impl);
    ethos_benchmark_reset();
    uint32_t start = osKernelGetSysTimerCount();
    auto result = module->forward(tensor);
    *ticks = osKernelGetSysTimerCount() - start;
    if (!result.ok()) return -500 - int(result.error());
    if (result->size() != 1 || !result->front().isTensor()) return -601;
    Tensor out = result->front().toTensor();
    if (out.scalar_type() != ScalarType::Char || out.dim() != 2 ||
        out.size(0) != 1 || out.size(1) != KWS_OUTPUT_ELEMENTS) return -602;
    std::memcpy(output, out.const_data_ptr<int8_t>(), KWS_OUTPUT_ELEMENTS);
    if (ethos_benchmark.invocations != 1 || !ethos_benchmark.cycles ||
        ethos_benchmark.overflow || ethos_benchmark.cycles != ethos_benchmark.active_cycles) return -603;
    return 0;
}
int test_network()
{
    module = arm::embedded::initialize_runtime(model_pte, model_pte_size,
        method_pool, sizeof(method_pool), temp_pool, sizeof(temp_pool));
    if (!module) return -610;
    auto load = module->load_method("forward");
    if (load != executorch::runtime::Error::Ok) return -700 - int(load);
    uint32_t service_error = 0;
    if (SERVICES_clocks_setting_get(se_services_s_handle, CLOCK_SETTING_EXTSYS0_FREQ,
            &kws_results.cpu_hz, &service_error) != SERVICES_REQ_SUCCESS || service_error) return -611;
    if (SERVICES_clocks_setting_get(se_services_s_handle, CLOCK_SETTING_AXI_FREQ,
            &kws_results.npu_hz, &service_error) != SERVICES_REQ_SUCCESS || service_error) return -612;
    kws_results.timer_hz = osKernelGetSysTimerFreq();
    if (!kws_results.cpu_hz || !kws_results.npu_hz || !kws_results.timer_hz) return -613;
    printf("KWS network-only test: 270 synthetic MFCC inputs, int8 NHWC [1,49,10,1] -> [1,12]\n");
    uint32_t ticks;
    for (unsigned test = 0; test < KWS_TEST_CASES; ++test) {
        std::memcpy(inference_input, kws_test_inputs[test], sizeof(inference_input));
        int error = forward(kws_outputs[test], &ticks);
        if (error) return error;
        unsigned de = max_error(kws_outputs[test], kws_tflite_default[test]);
        unsigned re = max_error(kws_outputs[test], kws_tflite_reference[test]);
        unsigned pe = max_error(kws_outputs[test], kws_pytorch[test]);
        if (de > kws_results.max_default_error) kws_results.max_default_error = de;
        if (re > kws_results.max_reference_error) kws_results.max_reference_error = re;
        if (pe > kws_results.max_pytorch_error) kws_results.max_pytorch_error = pe;
        unsigned predicted = top1(kws_outputs[test]);
        kws_results.default_top1_mismatches += predicted != top1(kws_tflite_default[test]);
        kws_results.reference_top1_mismatches += predicted != top1(kws_tflite_reference[test]);
        kws_results.pytorch_top1_mismatches += predicted != top1(kws_pytorch[test]);
        ++kws_results.cases;
        kws_progress = test + 1;
    }
    printf("KWS accuracy: cases=%lu, default max=%lu LSB, reference max=%lu LSB, pytorch max=%lu LSB, tolerance=%u LSB\n",
        (unsigned long)kws_results.cases, (unsigned long)kws_results.max_default_error,
        (unsigned long)kws_results.max_reference_error, (unsigned long)kws_results.max_pytorch_error, KWS_TOLERANCE_LSB);
    printf("KWS top1 mismatches: default=%lu, reference=%lu, pytorch=%lu\n",
        (unsigned long)kws_results.default_top1_mismatches, (unsigned long)kws_results.reference_top1_mismatches,
        (unsigned long)kws_results.pytorch_top1_mismatches);
    // Retain and report every board score so host verification can independently
    // recompute errors. UART is outside the timed benchmark loop below.
    for (unsigned test = 0; test < KWS_TEST_CASES; ++test) {
        printf("KWS output %u:", test);
        for (unsigned i = 0; i < KWS_OUTPUT_ELEMENTS; ++i) printf(" %d", int(kws_outputs[test][i]));
        printf("\n");
    }
    if (kws_results.max_default_error > KWS_TOLERANCE_LSB ||
        kws_results.max_reference_error > KWS_TOLERANCE_LSB ||
        kws_results.max_pytorch_error > KWS_TOLERANCE_LSB) return -620;
    kws_test_passed = 1;
    // Fixed locally generated uniform input random_0. Load/setup already done;
    // one immediate warmup, then 100 inferences with no UART or debugger halt.
    std::memcpy(inference_input, kws_test_inputs[KWS_BENCHMARK_CASE], sizeof(inference_input));
    int8_t output[KWS_OUTPUT_ELEMENTS];
    int error = forward(output, &ticks);
    if (error) return error;
    for (unsigned sample = 0; sample < KWS_BENCHMARK_SAMPLES; ++sample) {
        error = forward(output, &kws_forward_ticks[sample]);
        if (error) return error;
        kws_cycles[sample] = ethos_benchmark.cycles;
        if (max_error(output, kws_tflite_default[KWS_BENCHMARK_CASE]) > KWS_TOLERANCE_LSB) return -621;
    }
    auto usage = arm::embedded::runtime_memory_usage();
    kws_results.method_peak = usage.method_peak;
    kws_results.temp_peak = usage.temp_peak;
    printf("Benchmark clocks: CPU=%lu Hz, AXI/NPU=%lu Hz, timer=%lu Hz\n",
        (unsigned long)kws_results.cpu_hz, (unsigned long)kws_results.npu_hz, (unsigned long)kws_results.timer_hz);
    printf("Benchmark memory: model=%lu B, arena=%lu B at 0x%08lx, weights=%lu B at 0x%08lx\n",
        (unsigned long)model_pte_size, (unsigned long)ethos_benchmark.arena_size,
        (unsigned long)ethos_benchmark.arena_address, (unsigned long)ethos_benchmark.weights_size,
        (unsigned long)ethos_benchmark.weights_address);
    printf("Benchmark pools: method peak=%u B, temp peak=%u B, reserved=%u B\n",
        (unsigned)usage.method_peak, (unsigned)usage.temp_peak, (unsigned)(sizeof(method_pool)+sizeof(temp_pool)));
    for (unsigned sample = 0; sample < KWS_BENCHMARK_SAMPLES; ++sample)
        printf("Benchmark sample %u: NPU=%llu cycles (%.6f ms), forward=%lu ticks (%.6f ms)\n",
            sample, (unsigned long long)kws_cycles[sample],
            1000.0 * kws_cycles[sample] / kws_results.npu_hz, (unsigned long)kws_forward_ticks[sample],
            1000.0 * kws_forward_ticks[sample] / kws_results.timer_hz);
    printf("Benchmark PMU: last active=%llu cycles, invocations=%lu, overflow=%lu\n",
        (unsigned long long)ethos_benchmark.active_cycles, (unsigned long)ethos_benchmark.invocations,
        (unsigned long)ethos_benchmark.overflow);
    printf("Test_result: PASS\n");
    return 0;
}
}
extern "C" int app_main(void)
{
    int error = test_network();
    pipeline_error = error;
    app_result = error;
    if (error) printf("Test_result: FAIL (error %d, progress %lu)\n", error, (unsigned long)kws_progress);
    fflush(stdout);
    kws_benchmark_complete = 1; // Debugger completion marker; all timings finished.
    for (;;) { ++kws_heartbeat; osDelay(1000); }
}
