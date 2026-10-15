// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "embedded_runtime.hpp"
#include "nodes.h"
#include "board_nodes.h"
#include "spectrum.h"
#include "model_pte.h"
#include "model/model_test_data.h"
#include "cmsis_os2.h"
#include "main.h"
#include "se_services_port.h"

using namespace executorch::aten;
using arm::embedded::EmbeddedModule;

extern "C" {
volatile int32_t pipeline_error;
volatile uint32_t audio_blocks, spectrum_frames, frames_presented, audio_ticks_max;
volatile uint32_t audio_deadline_misses, lcd_deadline_misses, gain_test_passed;
volatile uint32_t spectrum_peak_hz;
uint64_t gain_benchmark_cycles[10];
uint32_t gain_benchmark_forward_ticks[10];
volatile uint32_t gain_benchmark_complete;
float spectrum_magnitude[SPECTRUM_BINS]; // Owned exclusively by the LCD thread.
#ifdef APP_MODEL_OSPI
volatile uint32_t model_verified, model_crc32;
#endif
}

namespace {
alignas(32) uint8_t method_pool[APP_METHOD_POOL_SIZE] __attribute__((section(APP_POOL_SECTION)));
alignas(32) uint8_t temp_pool[APP_TEMP_POOL_SIZE] __attribute__((section(APP_POOL_SECTION)));
alignas(32) float network_input[MICROPHONE_BLOCK_SAMPLES] __attribute__((section(APP_POOL_SECTION)));
alignas(32) uint16_t framebuffers[2][LCD_PIXELS] __attribute__((section(".bss.lcd_frame_buf")));
static uint64_t audio_stack[32768 / 8], lcd_stack[8192 / 8];
static float history[SPECTRUM_FFT_SIZE], window[SPECTRUM_FFT_SIZE];
alignas(16) static float fft_input[SPECTRUM_FFT_SIZE], fft_packed[SPECTRUM_FFT_SIZE];
static float gained[MICROPHONE_BLOCK_SAMPLES], magnitudes[SPECTRUM_BINS];
static arm_rfft_fast_instance_f32 fft;
static sliding_buffer_t sliding;
static float amplitude_scale;
static EmbeddedModule *module;
static uint32_t last_forward_ticks;

// A bounded, latest-value handoff. Audio never waits for rendering or VSYNC.
// RTX performs the copy while holding this short priority-inheriting mutex.
static osMutexId_t spectrum_mutex;
static float published[SPECTRUM_BINS];
static uint32_t published_peak;

void fail(int error)
{
    pipeline_error = error;
    app_result = error;
    // Retain the fault for live inspection without starving the other thread.
    osThreadExit();
}

int gain_forward(float *output)
{
    static std::array<SizesType, 4> sizes{1, 1, MICROPHONE_BLOCK_SAMPLES, 1};
    static std::array<DimOrderType, 4> order{0, 1, 2, 3};
    TensorImpl impl(ScalarType::Float, sizes.size(), sizes.data(), network_input, order.data());
    Tensor input(&impl);
    const uint32_t start = osKernelGetSysTimerCount();
    auto result = module->forward(input);
    last_forward_ticks = osKernelGetSysTimerCount() - start;
    if (!result.ok() || result->size() != 1 || !result->front().isTensor()) return -401;
    Tensor tensor = result->front().toTensor();
    if (tensor.scalar_type() != ScalarType::Float || tensor.numel() != MICROPHONE_BLOCK_SAMPLES) return -402;
    std::memcpy(output, tensor.const_data_ptr<float>(), sizeof(gained));
    return 0;
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
        model_pte_size, (unsigned long)ethos_benchmark.arena_size,
        (unsigned long)ethos_benchmark.arena_address, (unsigned long)ethos_benchmark.weights_size,
        (unsigned long)ethos_benchmark.weights_address);
    auto usage = arm::embedded::runtime_memory_usage();
    printf("Benchmark pools: method peak=%u B, temp peak=%u B, reserved=%u B\n",
        (unsigned)usage.method_peak, (unsigned)usage.temp_peak, (unsigned)(sizeof(method_pool) + sizeof(temp_pool)));
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

void audio_thread(void *)
{
    int16_t pcm[MICROPHONE_BLOCK_SAMPLES];
    int error = microphone_start();
    if (error) { fail(error); return; }
    for (;;) {
        bool gap;
        error = microphone_read(pcm, &gap);
        if (error) { fail(error); return; }
        uint32_t start = osKernelGetSysTimerCount();
        if (gap) sliding_buffer_init(&sliding, history, SPECTRUM_FFT_SIZE);
        pcm_to_float(pcm, network_input, MICROPHONE_BLOCK_SAMPLES);
        error = gain_forward(gained);
        if (error) { fail(error); return; }
        if (sliding_buffer_push(&sliding, gained, MICROPHONE_BLOCK_SAMPLES)) {
            remove_dc(history, fft_input, SPECTRUM_FFT_SIZE);
            apply_window(fft_input, window, fft_input, SPECTRUM_FFT_SIZE);
            fft_forward(&fft, fft_input, fft_packed);
            fft_magnitude(fft_packed, magnitudes, SPECTRUM_FFT_SIZE, amplitude_scale);
            unsigned peak = 1;
            for (unsigned i = 2; i < SPECTRUM_BINS; ++i)
                if (magnitudes[i] > magnitudes[peak]) peak = i;
            // If the LCD is copying a snapshot, skip this publication.
            if (osMutexAcquire(spectrum_mutex, 0) == osOK) {
                std::memcpy(published, magnitudes, sizeof(published));
                published_peak = peak * SPECTRUM_SAMPLE_RATE / SPECTRUM_FFT_SIZE;
                osMutexRelease(spectrum_mutex);
            }
            ++spectrum_frames;
        }
        ++audio_blocks;
        uint32_t elapsed = osKernelGetSysTimerCount() - start;
        if (elapsed > audio_ticks_max) audio_ticks_max = elapsed;
        if (elapsed > (uint64_t)osKernelGetSysTimerFreq() * MICROPHONE_BLOCK_SAMPLES / SPECTRUM_SAMPLE_RATE)
            ++audio_deadline_misses;
    }
}

void lcd_thread(void *)
{
    unsigned back = 1;
    bool held = false, logarithmic = true;
    uint32_t last_contact = 0, next = osKernelGetTickCount();
    const uint32_t period = osKernelGetTickFreq() / 20U;
    for (;;) {
        unsigned x = 0, y = 0;
        bool contact = false;
        int error = lcd_touch(&x, &y, &contact);
        if (error) { fail(error); return; }
        uint32_t now = osKernelGetTickCount();
        if (contact) {
            if (spectrum_touch(x, y, !held)) logarithmic = !logarithmic;
            held = true;
            last_contact = now;
        } else if (now - last_contact > osKernelGetTickFreq() * 150U / 1000U) {
            held = false;
            spectrum_touch_release();
        }
        if (osMutexAcquire(spectrum_mutex, osWaitForever) != osOK) { fail(-405); return; }
        std::memcpy(spectrum_magnitude, published, sizeof(published));
        spectrum_peak_hz = published_peak;
        osMutexRelease(spectrum_mutex);
        spectrum_render(framebuffers[back], logarithmic);
        error = lcd_present(framebuffers[back]);
        if (error) { fail(error); return; }
        ++frames_presented;
        back ^= 1U;
        next += period;
        if ((int32_t)(next - osKernelGetTickCount()) > 0) osDelayUntil(next);
        else { ++lcd_deadline_misses; next = osKernelGetTickCount(); }
    }
}
}

extern "C" int app_main(void)
{
#ifdef APP_MODEL_OSPI
    // app_prepare enabled XIP. Keep checking the separately programmed model.
    uint32_t crc = 0xffffffffU;
    for (size_t i = 0; i < model_pte_size; ++i) {
        crc ^= model_pte[i];
        for (unsigned b = 0; b < 8; ++b) crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    model_crc32 = ~crc;
    if (model_pte_size != MODEL_PTE_SIZE_BYTES || model_crc32 != MODEL_PTE_CRC32) return pipeline_error = -311;
    model_verified = 1;
#endif
    module = arm::embedded::initialize_runtime(model_pte, model_pte_size,
        method_pool, sizeof(method_pool), temp_pool, sizeof(temp_pool));
    if (!module) return pipeline_error = -400;
    int error = gain_self_test();
    if (error) return pipeline_error = error;
    sliding_buffer_init(&sliding, history, SPECTRUM_FFT_SIZE);
    amplitude_scale = hann_window_init(window, SPECTRUM_FFT_SIZE);
    if (fft_2048_init(&fft)) return pipeline_error = -406;
    const osMutexAttr_t mutex_attr = {"Spectrum", osMutexPrioInherit, nullptr, 0};
    spectrum_mutex = osMutexNew(&mutex_attr);
    if (!spectrum_mutex) return pipeline_error = -407;
    std::memset(published, 0, sizeof(published));
    spectrum_render(framebuffers[0], true);
    error = lcd_init(framebuffers[0]);
    if (error) return pipeline_error = error;
    osThreadAttr_t lcd_attr{};
    lcd_attr.name = "LCD animation"; lcd_attr.priority = osPriorityHigh;
    lcd_attr.stack_mem = lcd_stack; lcd_attr.stack_size = sizeof(lcd_stack);
    osThreadAttr_t audio_attr{};
    audio_attr.name = "Audio + Ethos"; audio_attr.priority = osPriorityRealtime;
    audio_attr.stack_mem = audio_stack; audio_attr.stack_size = sizeof(audio_stack);
    if (!osThreadNew(lcd_thread, nullptr, &lcd_attr)) return pipeline_error = -408;
    if (!osThreadNew(audio_thread, nullptr, &audio_attr)) return pipeline_error = -409;
    return 0;
}
