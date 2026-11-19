// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "app_config.hpp"
#include "stream_platform_config.hpp"
#include "EventQueue.hpp"
#include "scheduler_app.h"
#include "DtlnMethod.hpp"
#include "dtln_test_pattern.hpp"
#include "DtlnFakeSource.hpp"
#include <cassert>
#include <complex>
#include <cstdio>
#include <limits>
#include <memory_resource>
#include "arm_math.h"

std::pmr::synchronized_pool_resource pool;
uint32_t test_tick = 0, test_iterations = 0, test_limit = 628;
bool test_delay_race = false, test_delay_error = false;
void* arm_cmsis_stream::EventQueue::handlerData = nullptr;
arm_cmsis_stream::EventQueue::AppHandler arm_cmsis_stream::EventQueue::handler = nullptr;
std::atomic<bool> arm_cmsis_stream::EventQueue::handlerReady_ = false;

// Independent radix-two host FFT checks production packing/masking/inverse
// and generated scheduling. CMSIS-DSP numerical accuracy needs the board.
void arm_rfft_fast_f32(const arm_rfft_fast_instance_f32* fft, float* input,
                        float* output, uint8_t inverse) {
    const unsigned n = fft->length;
    std::vector<std::complex<double>> values(n);
    if (inverse) {
        values[0] = input[0];
        values[n / 2] = input[1];
        for (unsigned i = 1; i < n / 2; ++i) {
            values[i] = {input[2 * i], input[2 * i + 1]};
            values[n - i] = std::conj(values[i]);
        }
    } else {
        for (unsigned i = 0; i < n; ++i) values[i] = input[i];
    }
    for (unsigned i = 1, j = 0; i < n; ++i) {
        unsigned bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(values[i], values[j]);
    }
    for (unsigned size = 2; size <= n; size <<= 1) {
        const double angle = (inverse ? 1 : -1) * 6.28318530717958647692 / size;
        const std::complex<double> step{std::cos(angle), std::sin(angle)};
        for (unsigned base = 0; base < n; base += size) {
            std::complex<double> phase{1, 0};
            for (unsigned j = 0; j < size / 2; ++j) {
                const auto a = values[base + j], b = values[base + j + size / 2] * phase;
                values[base + j] = a + b;
                values[base + j + size / 2] = a - b;
                phase *= step;
            }
        }
    }
    if (inverse) {
        for (unsigned i = 0; i < n; ++i) output[i] = float(values[i].real() / n);
    } else {
        output[0] = float(values[0].real());
        output[1] = float(values[n / 2].real());
        for (unsigned i = 1; i < n / 2; ++i) {
            output[2 * i] = float(values[i].real());
            output[2 * i + 1] = float(values[i].imag());
        }
    }
    // Real CMSIS-DSP fast RFFT is destructive. Poison input after observing
    // its values to detect a shared time-window branch passed directly to it.
    std::fill(input, input + n, 99.0f);
}
extern "C" void fft_forward(arm_rfft_fast_instance_f32* fft, float* in, float* out) {
    arm_rfft_fast_f32(fft, in, out, 0);
}
static uint32_t calls[2]{};
static bool inject_nan;
static int fake_forward(unsigned stage, const float* signal, const float* state,
                         const float* reference, float* output, float* next,
                         DtlnForwardTiming* timing) {
    assert(stage == 1 || stage == 2);
    constexpr unsigned episode_hops = dtln::TestPattern::cycle_samples / DTLN_AUDIO_HOP_SAMPLES;
    assert(state[0] == float(calls[stage - 1] % episode_hops));
    ++calls[stage - 1];
    const unsigned size = stage == 1 ? 257 : 512;
    if (stage == 2) {
        static const dtln::TestPattern pattern;
        for (unsigned i = 0; i < size; ++i) {
            const int index = int((calls[1] - 1) * 128 + i) - 384;
            const float expected = index < 0 ? 0.0f : pattern.at(unsigned(index)).reference;
            assert(std::fabs(reference[i] - expected) < 1e-7f);
        }
    }
    for (unsigned i = 0; i < size; ++i) {
        assert(std::isfinite(signal[i]) && std::isfinite(reference[i]));
        output[i] = stage == 1 ? 1.0f : 0.25f * signal[i];
    }
    for (unsigned i = 0; i < 512; ++i) next[i] = state[i] + 1.0f;
    *timing = {100, 90};
    if (inject_nan) output[0] = std::numeric_limits<float>::quiet_NaN();
    return 0;
}
class Queue : public arm_cmsis_stream::EventQueue {
    bool push(arm_cmsis_stream::Message&&) final { return true; }
    bool isEmpty() final { return true; }
    void clear() final {}
    void execute() final {}
};

int main() {
    StreamStats stats{};
    HardwareParams hardware{&stats, fake_forward};
    app_params_set_hardware(&hardware);
    Queue queue;
    assert(init_scheduler_app(&queue, &appParams) == CG_SUCCESS);
    int error = 0;
    scheduler_app(&error);
    assert(error == CG_STOP_SCHEDULER);
    assert(stats.output_blocks == 628 && stats.audio_blocks == 628);
    assert(stats.stage1_calls == 628 && stats.stage2_calls == 628);
    assert(stats.input_epoch == 2);
    assert(stats.cycles_completed == 1 && stats.nonfinite_samples == 0);
    assert(stats.deadline_misses == 0 && stats.source_late_frames == 0);
    assert(stats.cases[0].samples == 8000 && stats.cases[4].samples == 8000);
    for (const auto& value : stats.cases) {
        assert(std::fabs(value.output_rms - value.input_rms) < 1e-6);
    }
    assert(stats.cases[0].output_rms < 1e-7 && stats.cases[4].output_rms < 1e-7);
    free_scheduler_app();
    // Fast-math-safe finite guard must reject a bad model before the DSP.
    calls[0] = 0;
    float a[257]{}, b[257]{}, result[257]{};
    arm_cmsis_stream::FIFO<float,257,1,0> fa(a), fb(b), fo(result);
    DtlnMethod<float,257,float,257,float,257> method(fa, fb, fo, {hardware, 1});
    assert(method.init() == CG_SUCCESS);
    inject_nan = true;
    assert(method.run() == -511);
    float source_mic[128]{}, source_ref[128]{};
    arm_cmsis_stream::FIFO<float,128,1,0> fm(source_mic), fr(source_ref);
    DtlnFakeSource<float,128,float,128> source(fm, fr, {hardware});
    assert(source.init() == CG_SUCCESS && source.run() == CG_SUCCESS);
    test_tick += 20;
    const auto late = stats.source_late_frames;
    const auto before = test_tick;
    assert(source.run() == CG_SUCCESS);
    assert(stats.source_late_frames == late + 1 && test_tick == before + 1);
    test_delay_race = true;
    assert(source.run() == CG_SUCCESS);
    assert(stats.source_late_frames == late + 2);
    test_delay_race = false;
    test_delay_error = true;
    assert(source.run() == CG_OS_ERROR);
    printf("DTLN generated scheduler PASS, 628 hops, aligned overlap-add, independent states, nonfinite guard\n");
}
