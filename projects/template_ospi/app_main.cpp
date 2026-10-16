// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include <cmath>
#include <cstdio>
#include <cstring>
#include "embedded_runtime.hpp"
#include "board_nodes.h"
#include "spectrum.h"
#include "model/model_test_data.h"
#include "cmsis_os2.h"
#include "main.h"
#include "se_services_port.h"
#include "stream_init.hpp"

extern "C" {
volatile int32_t pipeline_error;
volatile uint32_t gain_test_passed;
uint64_t gain_benchmark_cycles[10];
uint32_t gain_benchmark_forward_ticks[10];
volatile uint32_t gain_benchmark_complete;
}
namespace {
HardwareParams hardware;
float *network_input;
float gained[MIC_BLOCK_SAMPLES];
uint32_t last_forward_ticks;
int gain_forward(float *output)
{
    return arm::embedded::gain_forward(static_cast<arm::embedded::EmbeddedModule*>(hardware.module),
        network_input, output, MIC_BLOCK_SAMPLES, &last_forward_ticks);
}
int gain_self_test()
{
    uint32_t cpu_hz = 0, axi_hz = 0, service_error = 0;
    if (SERVICES_clocks_setting_get(se_services_s_handle, CLOCK_SETTING_EXTSYS0_FREQ,
            &cpu_hz, &service_error) != SERVICES_REQ_SUCCESS || service_error || !cpu_hz) return -410;
    if (SERVICES_clocks_setting_get(se_services_s_handle, CLOCK_SETTING_AXI_FREQ,
            &axi_hz, &service_error) != SERVICES_REQ_SUCCESS || service_error || !axi_hz) return -410;
    const uint32_t timer_hz = osKernelGetSysTimerFreq();
    if (!timer_hz) return -410;
    for (unsigned i = 0; i < MICROPHONE_BLOCK_SAMPLES; ++i)
        network_input[i] = -1.0f + 2.0f * i / (MICROPHONE_BLOCK_SAMPLES - 1U);
    float max_error = 0;
    // One method/cache warmup, followed by ten measured, checked forwards.
    // Capture and LCD rendering have not started, so they cannot affect timing.
    for (unsigned sample = 0; sample <= 10; ++sample) {
        ethos_benchmark_reset();
        int error = gain_forward(gained);
        if (error) return error;
        if (ethos_benchmark.invocations != 1 || !ethos_benchmark.cycles || ethos_benchmark.overflow ||
                ethos_benchmark.cycles != ethos_benchmark.active_cycles) return -403;
        if (sample) {
            gain_benchmark_cycles[sample - 1] = ethos_benchmark.cycles;
            gain_benchmark_forward_ticks[sample - 1] = last_forward_ticks;
        }
        for (unsigned i = 0; i < MICROPHONE_BLOCK_SAMPLES; ++i) {
            uint32_t bits;
            std::memcpy(&bits, gained + i, sizeof(bits));
            float error = std::fabs(gained[i] - model_gain * network_input[i]);
            if ((bits & 0x7f800000U) == 0x7f800000U || error > model_test_tolerance) return -404;
            if (error > max_error) max_error = error;
        }
    }
    gain_test_passed = 1;
    printf("Ethos gain self-test: PASS (512 signed samples, gain %.1f)\n", model_gain);
    printf("Output: 512 elements, max absolute error %.6f\n", max_error);
    printf("Benchmark clocks: CPU=%lu Hz, AXI/NPU=%lu Hz, timer=%lu Hz\n",
        (unsigned long)cpu_hz, (unsigned long)axi_hz, (unsigned long)timer_hz);
    printf("Benchmark memory: model=%lu B, arena=%lu B at 0x%08lx, weights=%lu B at 0x%08lx\n",
        (unsigned long)hardware.model_size, (unsigned long)ethos_benchmark.arena_size,
        (unsigned long)ethos_benchmark.arena_address, (unsigned long)ethos_benchmark.weights_size,
        (unsigned long)ethos_benchmark.weights_address);
    auto usage = arm::embedded::runtime_memory_usage();
    printf("Benchmark pools: method peak=%u B, temp peak=%u B, reserved=%u B\n",
        (unsigned)usage.method_peak, (unsigned)usage.temp_peak, (unsigned)(hardware.method_pool_size + hardware.temp_pool_size));
    for (unsigned sample = 0; sample < 10; ++sample)
        printf("Benchmark sample %u: NPU=%llu cycles (%.6f ms), forward=%lu ticks (%.6f ms)\n",
            sample + 1, (unsigned long long)gain_benchmark_cycles[sample],
            1000.0 * gain_benchmark_cycles[sample] / axi_hz,
            (unsigned long)gain_benchmark_forward_ticks[sample],
            1000.0 * gain_benchmark_forward_ticks[sample] / timer_hz);
    printf("Benchmark PMU: last active=%llu cycles, invocations=%lu, overflow=%lu\n",
        (unsigned long long)ethos_benchmark.active_cycles, (unsigned long)ethos_benchmark.invocations,
        (unsigned long)ethos_benchmark.overflow);
    gain_benchmark_complete = 1;
    printf("Test_result: PASS\n");
    fflush(stdout);
    return 0;
}

}
extern "C" int app_main(void)
{
    int error = hardware_params_init(&hardware);
    if (!error) {
        network_input = hardware.inference_input;
        error = gain_self_test();
    }
    if (!error) error = stream_configure_and_start(&hardware);
    if (error) { stream_stats.error = error; pipeline_error = error; }
    return error;
}
