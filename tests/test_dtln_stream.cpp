// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "app_config.hpp"
#include "stream_platform_config.hpp"
#include "EventQueue.hpp"
#include "scheduler_app.h"
#include "DtlnMethod.hpp"
#include <array>
#include <cassert>
#include <complex>
#include <cstdio>
#include <fstream>
#include <limits>
#include <memory_resource>
#include "arm_math.h"

std::pmr::synchronized_pool_resource pool;
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
constexpr size_t hop = DTLN_AUDIO_HOP_SAMPLES;
constexpr size_t delay = DTLN_AUDIO_FRAME_SAMPLES - hop;
using StereoHop = std::array<float, 2 * hop>;
using MonoHop = std::array<float, hop>;
static std::vector<StereoHop> inputRecords;
static std::vector<MonoHop> outputRecords;
static size_t nextRecord;
static uint32_t timeslot;
SdsSourceParams dtlnReplayParams;
SdsSinkParams dtlnRecordParams;

extern "C" int32_t sdsRead(sdsId_t id, uint32_t* slot, void* output, uint32_t bytes) {
    assert(id == &inputRecords && bytes == sizeof(StereoHop));
    if (nextRecord == inputRecords.size()) return SDS_EOS;
    *slot = uint32_t(nextRecord * 8);
    std::memcpy(output, inputRecords[nextRecord++].data(), bytes);
    return int32_t(bytes);
}

extern "C" int32_t sdsWrite(sdsId_t id, uint32_t slot, const void* input, uint32_t bytes) {
    assert(id == &outputRecords && bytes == sizeof(MonoHop));
    assert(slot == outputRecords.size() * 8);
    outputRecords.emplace_back();
    std::memcpy(outputRecords.back().data(), input, bytes);
    return int32_t(bytes);
}

static uint32_t recordTimeslot(void*) { return timeslot; }

static float inputSample(int index, size_t channel) {
    if (index < 0) return 0.0f;
    return inputRecords[index / hop][2 * (index % hop) + channel];
}

static int fake_forward(unsigned stage, const float* signal, const float* state,
                         const float* reference, float* output, float* next,
                         DtlnForwardTiming* timing) {
    assert(stage == 1 || stage == 2);
    assert(state[0] == float(calls[stage - 1]));
    ++calls[stage - 1];
    const unsigned size = stage == 1 ? 257 : 512;
    if (stage == 2 && !inject_nan) {
        for (unsigned i = 0; i < size; ++i) {
            const int index = int((calls[1] - 1) * hop + i) - int(delay);
            assert(std::fabs(reference[i] - inputSample(index, 1)) < 1e-7f);
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
static void runGraph() {
    calls[0] = calls[1] = 0;
    nextRecord = 0;
    timeslot = 0;
    outputRecords.clear();
    dtlnReplayParams = {&inputRecords, &timeslot};
    dtlnRecordParams = {&outputRecords, recordTimeslot};
    StreamStats stats{};
    HardwareParams hardware{&stats, fake_forward};
    app_params_set_hardware(&hardware);
    assert(init_scheduler_app(nullptr, &appParams) == CG_SUCCESS);
    reset_fifos_scheduler_app(1);
    int error = 0;
    assert(scheduler_app(&error) == inputRecords.size());
    assert(error == CG_STOP_SCHEDULER);
    assert(outputRecords.size() == inputRecords.size());
    assert(stats.stage1_calls == inputRecords.size() && stats.stage2_calls == inputRecords.size());
    // Unity mask, inverse FFT and one-quarter gain overlap-add to the input.
    for (size_t frame = 0; frame < outputRecords.size(); ++frame) {
        for (size_t i = 0; i < hop; ++i) {
            const int index = int(frame * hop + i) - int(delay);
            assert(std::fabs(outputRecords[frame][i] - inputSample(index, 0)) < 1e-6f);
        }
    }
    free_scheduler_app();
}

static void loadReplay(const char* name) {
    std::ifstream file(name, std::ios::binary);
    assert(file);
    inputRecords.clear();
    while (file.peek() != std::char_traits<char>::eof()) {
        uint32_t header[2];
        assert(file.read(reinterpret_cast<char*>(header), sizeof(header)));
        assert(header[0] == inputRecords.size() * 8 && header[1] == sizeof(StereoHop));
        inputRecords.emplace_back();
        assert(file.read(reinterpret_cast<char*>(inputRecords.back().data()), sizeof(StereoHop)));
    }
    assert(!inputRecords.empty());
}

int main(int argc, char** argv) {
    inputRecords.resize(12);
    for (size_t frame = 0; frame < inputRecords.size(); ++frame) {
        for (size_t i = 0; i < hop; ++i) {
            inputRecords[frame][2 * i] = float(i % 5) / 50.0f;
            inputRecords[frame][2 * i + 1] = -float((frame + i) % 7) / 60.0f;
        }
    }
    runGraph();
    for (int i = 1; i < argc; ++i) {
        loadReplay(argv[i]);
        runGraph();
        printf("Verified %s, %zu SDS hops\n", argv[i], outputRecords.size());
    }
    // Fast-math-safe finite guard must reject a bad model before the DSP.
    calls[0] = 0;
    StreamStats stats{};
    HardwareParams hardware{&stats, fake_forward};
    float a[257]{}, b[257]{}, result[257]{};
    arm_cmsis_stream::FIFO<float,257,1,0> fa(a), fb(b), fo(result);
    DtlnMethod<float,257,float,257,float,257> method(fa, fb, fo, {hardware, 1});
    assert(method.init() == CG_SUCCESS);
    inject_nan = true;
    assert(method.run() == -511);
    printf("DTLN SDS scheduler PASS, channel ordering, timeslots, overlap-add and fresh model states\n");
}
