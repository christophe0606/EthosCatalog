// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
// Network-only correctness and timing; no camera/LCD/CMSIS Stream execution.
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
volatile uint32_t detection_test_passed, detection_benchmark_complete, detection_progress, detection_heartbeat;
uint64_t detection_cycles[DETECTION_BENCHMARK_SAMPLES];
uint32_t detection_forward_ticks[DETECTION_BENCHMARK_SAMPLES];
int8_t detection_outputs0[DETECTION_TEST_CASES][DETECTION_OUTPUT0_ELEMENTS];
int8_t detection_outputs1[DETECTION_TEST_CASES][DETECTION_OUTPUT1_ELEMENTS];
struct DetectionResults { uint32_t cases, cpu_hz, npu_hz, timer_hz, method_peak, temp_peak; } detection_results;
}
namespace {
alignas(32) uint8_t method_pool[APP_METHOD_POOL_SIZE] __attribute__((section(APP_POOL_SECTION)));
alignas(32) uint8_t temp_pool[APP_TEMP_POOL_SIZE] __attribute__((section(APP_POOL_SECTION)));
alignas(32) int8_t inference_input[DETECTION_INPUT_ELEMENTS] __attribute__((section(APP_POOL_SECTION)));
arm::embedded::EmbeddedModule *module;
unsigned compare(const int8_t *actual, const int8_t *expected, unsigned count, unsigned *sum)
{
    unsigned maximum = 0; *sum = 0;
    for (unsigned i = 0; i < count; ++i) {
        int delta = int(actual[i]) - int(expected[i]);
        unsigned error = delta < 0 ? unsigned(-delta) : unsigned(delta);
        if (error > maximum) maximum = error;
        *sum += error;
    }
    return maximum;
}
int forward(int8_t *output0, int8_t *output1, uint32_t *ticks, arm::embedded::EmbeddedModule *runtime = nullptr)
{
    using namespace executorch::aten;
    std::array<SizesType, 4> sizes{1, 192, 192, 1};
    std::array<DimOrderType, 4> order{0, 1, 2, 3};
    TensorImpl impl(ScalarType::Char, sizes.size(), sizes.data(), inference_input, order.data());
    Tensor tensor(&impl);
    ethos_benchmark_reset();
    uint32_t start = osKernelGetSysTimerCount();
    auto result = (runtime ? runtime : module)->forward(tensor);
    *ticks = osKernelGetSysTimerCount() - start;
    if (!result.ok()) return -500 - int(result.error());
    if (result->size() != 2) return -601;
    for (unsigned head = 0; head < 2; ++head) {
        if (!(*result)[head].isTensor()) return -602;
        Tensor out = (*result)[head].toTensor();
        unsigned grid = head ? 12 : 6;
        if (out.scalar_type() != ScalarType::Char || out.dim() != 4 ||
            out.size(0) != 1 || out.size(1) != grid || out.size(2) != grid || out.size(3) != 18) return -603;
        std::memcpy(head ? output1 : output0, out.const_data_ptr<int8_t>(), grid * grid * 18);
    }
    if (ethos_benchmark.invocations != 1 || !ethos_benchmark.cycles || ethos_benchmark.overflow ||
        ethos_benchmark.cycles != ethos_benchmark.active_cycles) return -604;
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
            &detection_results.cpu_hz, &service_error) != SERVICES_REQ_SUCCESS || service_error) return -611;
    if (SERVICES_clocks_setting_get(se_services_s_handle, CLOCK_SETTING_AXI_FREQ,
            &detection_results.npu_hz, &service_error) != SERVICES_REQ_SUCCESS || service_error) return -612;
    detection_results.timer_hz = osKernelGetSysTimerFreq();
    if (!detection_results.cpu_hz || !detection_results.npu_hz || !detection_results.timer_hz) return -613;
    printf("Detection network-only test: 4 int8 NHWC [1,192,192,1] -> [1,6,6,18], [1,12,12,18]\n");
    uint32_t ticks;
    bool valid = true;
    for (unsigned test = 0; test < DETECTION_TEST_CASES; ++test) {
        std::memcpy(inference_input, detection_test_inputs[test], sizeof(inference_input));
        int error = forward(detection_outputs0[test], detection_outputs1[test], &ticks);
        if (error) return error;
        for (unsigned head = 0; head < 2; ++head) {
            const int8_t *actual = head ? detection_outputs1[test] : detection_outputs0[test];
            unsigned count = head ? DETECTION_OUTPUT1_ELEMENTS : DETECTION_OUTPUT0_ELEMENTS;
            const int8_t *refs[] = {head ? detection_tflite_default1[test] : detection_tflite_default0[test],
                head ? detection_tflite_reference1[test] : detection_tflite_reference0[test],
                head ? detection_pytorch1[test] : detection_pytorch0[test]};
            const char *names[] = {"default", "reference", "pytorch"};
            for (unsigned r = 0; r < 3; ++r) {
                unsigned sum;
                unsigned maximum = compare(actual, refs[r], count, &sum);
                printf("Detection compare %u %u %s: max=%u sum=%u count=%u\n", test, head, names[r], maximum, sum, count);
                if (r == 0 && (maximum > DETECTION_TOLERANCE_LSB)) valid = false;
            }
            for (unsigned offset = 0; offset < count; offset += 54) {
                printf("Detection output %u %u %u:", test, head, offset);
                for (unsigned i = offset; i < offset + 54 && i < count; ++i) printf(" %d", int(actual[i]));
                printf("\n");
            }
        }
        ++detection_results.cases; detection_progress = test + 1;
    }
    if (!valid) return -620;
    detection_test_passed = 1;
    std::memcpy(inference_input, detection_test_inputs[DETECTION_BENCHMARK_CASE], sizeof(inference_input));
    // One immediate warmup after fixture validation; all output copies/checks
    // and UART reporting occur outside module.forward() timing.
    int error = forward(detection_outputs0[DETECTION_BENCHMARK_CASE], detection_outputs1[DETECTION_BENCHMARK_CASE], &ticks);
    if (error) return error;
    for (unsigned sample = 0; sample < DETECTION_BENCHMARK_SAMPLES; ++sample) {
        error = forward(detection_outputs0[DETECTION_BENCHMARK_CASE], detection_outputs1[DETECTION_BENCHMARK_CASE], &detection_forward_ticks[sample]);
        if (error) return error;
        detection_cycles[sample] = ethos_benchmark.cycles;
        unsigned sum;
        if (compare(detection_outputs0[DETECTION_BENCHMARK_CASE], detection_tflite_default0[DETECTION_BENCHMARK_CASE], DETECTION_OUTPUT0_ELEMENTS, &sum) > DETECTION_TOLERANCE_LSB ||
            compare(detection_outputs1[DETECTION_BENCHMARK_CASE], detection_tflite_default1[DETECTION_BENCHMARK_CASE], DETECTION_OUTPUT1_ELEMENTS, &sum) > DETECTION_TOLERANCE_LSB) return -621;
    }
    auto usage = arm::embedded::runtime_memory_usage();
    detection_results.method_peak = usage.method_peak; detection_results.temp_peak = usage.temp_peak;
    printf("Benchmark clocks: CPU=%lu Hz, AXI/NPU=%lu Hz, timer=%lu Hz\n",
        (unsigned long)detection_results.cpu_hz, (unsigned long)detection_results.npu_hz, (unsigned long)detection_results.timer_hz);
    printf("Benchmark memory: model=%lu B, arena=%lu B at 0x%08lx, weights=%lu B at 0x%08lx\n",
        (unsigned long)model_pte_size, (unsigned long)ethos_benchmark.arena_size, (unsigned long)ethos_benchmark.arena_address,
        (unsigned long)ethos_benchmark.weights_size, (unsigned long)ethos_benchmark.weights_address);
    printf("Benchmark pools: method peak=%u B, temp peak=%u B, reserved=%u B\n",
        (unsigned)usage.method_peak, (unsigned)usage.temp_peak, (unsigned)(sizeof(method_pool)+sizeof(temp_pool)));
    for (unsigned sample = 0; sample < DETECTION_BENCHMARK_SAMPLES; ++sample)
        printf("Benchmark sample %u: NPU=%llu cycles (%.6f ms), forward=%lu ticks (%.6f ms)\n", sample,
            (unsigned long long)detection_cycles[sample], 1000.0 * detection_cycles[sample] / detection_results.npu_hz,
            (unsigned long)detection_forward_ticks[sample], 1000.0 * detection_forward_ticks[sample] / detection_results.timer_hz);
    printf("Benchmark PMU: last active=%llu cycles, invocations=%lu, overflow=%lu\n",
        (unsigned long long)ethos_benchmark.active_cycles, (unsigned long)ethos_benchmark.invocations, (unsigned long)ethos_benchmark.overflow);
    printf("Test_result: PASS\n");
    return 0;
}
}
#ifndef DETECTION_DEMO
extern "C" int app_main(void)
{
    int error = test_network(); pipeline_error = error; app_result = error;
    if (error) printf("Test_result: FAIL (error %d, progress %lu)\n", error, (unsigned long)detection_progress);
    fflush(stdout); detection_benchmark_complete = 1;
    for (;;) { ++detection_heartbeat; osDelay(1000); }
}
#endif
