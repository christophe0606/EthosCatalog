// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "app_config.hpp"
#include "stream_platform_config.hpp"
#include "stream_runtime_init.hpp"
#include "DtlnMethod.hpp"
#include "peripherals.hpp"
#include "stream_init.hpp"
#include <array>
#include <cassert>
#include <complex>
#include <cstdio>
#include <fstream>
#include <limits>
#include <thread>
#include "arm_math.h"

uint32_t osEventFlagsWait(osEventFlagsId_t& event, uint32_t mask, uint32_t, uint32_t timeout) {
    const auto flags = event.wait(mask, true, timeout);
    return flags ? flags : osFlagsError;
}

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
using SpeakerBlock = std::array<int16_t, DtlnSpeaker::blockFrames>;
static std::vector<SpeakerBlock> speakerRecords;
static size_t nextRecord;
static SdsPlaybackTiming playback;
SdsSourceParams dtlnReplayParams;
SdsSinkParams dtlnRecordParams;
static uint16_t lcdFrames[2][LCD_PIXELS];
static DtlnLcd lcd{{lcdFrames[0], lcdFrames[1]}};
static DtlnSpeaker speaker{};
static std::thread::id dataflowThread;
static unsigned lcdPresents;
static unsigned speakerDrains;
static unsigned audibleSpeakerDrains;
static bool speakerMuted = true;
extern uint8_t stream_app_buf0[], stream_app_buf1[], stream_app_buf2[];
extern uint8_t stream_app_buf3[], stream_app_buf4[], stream_app_buf5[];
static const std::array fifoStorage{stream_app_buf0, stream_app_buf1, stream_app_buf2,
                                    stream_app_buf3, stream_app_buf4, stream_app_buf5};
static uint32_t runtimeEventsOnFirstRead;
static bool sdsStopOnFirstRead;
static sdsId_t replayHandle;
static sdsId_t recordHandle;
static unsigned expectedClipsOnThread;
static unsigned clipsOnThread;
static unsigned clipsOnEventThread;

bool dtlnPlaybackStopping() { return sdsStopOnFirstRead && nextRecord != 0; }
void dtlnRecordHopTicks(uint32_t) {}

extern "C" int32_t sdsRead(sdsId_t id, uint32_t* slot, void* output, uint32_t bytes) {
    assert(id == replayHandle && bytes == sizeof(StereoHop));
    if (nextRecord == 0) {
        assert(!speakerMuted);
        static thread_local unsigned clips;
        clipsOnThread = ++clips;
        dataflowThread = std::this_thread::get_id();
        cg_streamEvent.post(runtimeEventsOnFirstRead);
        for (const auto* buffer : fifoStorage) assert(buffer[0] == 0);
    }
    if (nextRecord == inputRecords.size()) return SDS_EOS;
    *slot = uint32_t(nextRecord * 8);
    std::memcpy(output, inputRecords[nextRecord++].data(), bytes);
    return int32_t(bytes);
}

extern "C" int32_t sdsWrite(sdsId_t id, uint32_t slot, const void* input, uint32_t bytes) {
    assert(id == recordHandle && bytes == sizeof(MonoHop));
    assert(slot == outputRecords.size() * 8);
    assert(osKernelGetTickCount() == outputRecords.size() * 8);
    outputRecords.emplace_back();
    std::memcpy(outputRecords.back().data(), input, bytes);
    return int32_t(bytes);
}


extern "C" int lcd_present(uint16_t* frame) {
    assert(std::this_thread::get_id() != dataflowThread);
    assert(frame == lcd.frames[lcd.front ^ 1U]);
    assert(frame[0] == 0x0863);
    if (lcdPresents++ == 0) {
        static thread_local unsigned clips;
        clipsOnEventThread = ++clips;
    }
    return 0;
}

extern "C" int devkit_e8_audio_write(const int16_t* samples, unsigned frames) {
    assert(!speakerMuted);
    assert(frames == SpeakerBlock{}.size());
    std::copy_n(samples, frames, speakerRecords.emplace_back().begin());
    return 0;
}

extern "C" int devkit_e8_audio_drain(unsigned timeoutMs) {
    assert(timeoutMs == 200);
    ++speakerDrains;
    if (!speakerMuted) ++audibleSpeakerDrains;
    return 0;
}

extern "C" int devkit_e8_audio_set_muted(bool muted) {
    speakerMuted = muted;
    return 0;
}

static float inputSample(int index, size_t channel) {
    if (index < 0) return 0.0f;
    return inputRecords[index / hop][2 * (index % hop) + channel];
}

extern "C" int dtln_forward(unsigned stage, const float* signal, const float* state,
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
static void runGraph(uint32_t runtimeEvents = 0, bool sdsStop = false) {
    calls[0] = calls[1] = 0;
    nextRecord = 0;
    // POSIX retains the event objects across runs. RTX allocates fresh flags on startup.
    if (!expectedClipsOnThread) {
        cg_streamEvent.clear(UINT32_MAX);
        cg_streamReplyEvent.clear(UINT32_MAX);
    }
    for (auto* buffer : fifoStorage) buffer[0] = 0xa5;
    runtimeEventsOnFirstRead = runtimeEvents;
    sdsStopOnFirstRead = sdsStop;
    test_rtos::tick = 0;
    outputRecords.clear();
    speakerRecords.clear();
    speakerDrains = 0;
    audibleSpeakerDrains = 0;
    lcdPresents = 0;
    static std::array<char, 4> handles;
    const size_t handleIndex = (expectedClipsOnThread % 2) * 2;
    replayHandle = &handles[handleIndex];
    recordHandle = &handles[handleIndex + 1];
    dtlnReplayParams = {replayHandle, &playback, DTLN_AUDIO_SAMPLE_RATE, DTLN_AUDIO_HOP_SAMPLES};
    dtlnRecordParams = {recordHandle, &playback};
    static StreamStats stats;
    stats = {};
    static const HardwareParams hardware{&stats, &lcd, &speaker};
    const auto expected = (runtimeEvents & STREAM_PAUSE_EVENT) && !(runtimeEvents & STREAM_DONE_EVENT)
        ? CG_PAUSED_SCHEDULER : CG_STOP_SCHEDULER;
    assert(dtln_stream_start(hardware) == CG_SUCCESS);
    while (!dtln_stream_finished()) std::this_thread::yield();
    assert(dtln_stream_finish() == expected);
    assert(speakerMuted);
    assert(clipsOnThread == expectedClipsOnThread + 1);
    if (runtimeEvents || sdsStop) {
        assert(nextRecord == 1 && calls[0] == 0 && calls[1] == 0);
        assert(outputRecords.empty());
        assert(audibleSpeakerDrains == 0);
        return;
    }
    ++expectedClipsOnThread;
    assert(clipsOnEventThread == expectedClipsOnThread);
    assert(speakerDrains == 1 && audibleSpeakerDrains == 1);
    assert(outputRecords.size() == inputRecords.size());
    assert(!stats.lcd_error && stats.lcd_frames == lcdPresents && lcdPresents >= (inputRecords.empty() ? 1 : 2));
    assert(stats.stage1_calls == inputRecords.size() && stats.stage2_calls == inputRecords.size());
    // Unity mask, inverse FFT and one-quarter gain overlap-add to the input.
    for (size_t frame = 0; frame < outputRecords.size(); ++frame) {
        for (size_t i = 0; i < hop; ++i) {
            const int index = int(frame * hop + i) - int(delay);
            assert(std::fabs(outputRecords[frame][i] - inputSample(index, 0)) < 1e-6f);
        }
    }
    constexpr size_t blockFrames = DtlnSpeaker::blockFrames;
    const size_t sampleCount = outputRecords.size() * hop;
    assert(speakerRecords.size() == (sampleCount + blockFrames - 1) / blockFrames);
    for (size_t i = 0; i < speakerRecords.size() * blockFrames; ++i) {
        const float sample = i < sampleCount ? outputRecords[i / hop][i % hop] : 0.0f;
        const auto expected = static_cast<int16_t>(std::clamp(sample, -1.0f, 32767.0f / 32768.0f) * 32768.0f);
        assert(speakerRecords[i / blockFrames][i % blockFrames] == expected);
    }
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
    inputRecords.resize(13);
    for (size_t frame = 0; frame < inputRecords.size(); ++frame) {
        for (size_t i = 0; i < hop; ++i) {
            inputRecords[frame][2 * i] = float(i % 5) / 50.0f;
            inputRecords[frame][2 * i + 1] = -float((frame + i) % 7) / 60.0f;
        }
    }
    runGraph(STREAM_PAUSE_EVENT);
    runGraph(STREAM_DONE_EVENT);
    runGraph(STREAM_DONE_EVENT | STREAM_PAUSE_EVENT);
    runGraph(STREAM_PAUSE_EVENT, true);
    runGraph(0, true);
    runGraph();
    for (int i = 1; i < argc; ++i) {
        loadReplay(argv[i]);
        runGraph();
        printf("Verified %s, %zu SDS hops\n", argv[i], outputRecords.size());
    }
    // An immediate EOS must not pause dataflow ahead of the event thread's resume acknowledgement.
    inputRecords.clear();
    runGraph();
    dtln_stream_shutdown();
    // Fast-math-safe finite guard must reject a bad model before the DSP.
    calls[0] = 0;
    StreamStats stats{};
    HardwareParams hardware{&stats, &lcd, &speaker};
    float a[257]{}, b[257]{}, result[257]{};
    arm_cmsis_stream::FIFO<float,257,1,0> fa(a), fb(b), fo(result);
    DtlnMethod<float,257,float,257,float,257> method(fa, fb, fo, {hardware, 1});
    assert(method.init() == CG_SUCCESS);
    assert(method.run() == CG_SUCCESS);
    auto identified = createStreamNode(method);
    assert(identified.state_reset_intf);
    identified.state_reset_intf->reset(identified.obj);
    calls[0] = 0;
    inject_nan = true;
    assert(method.run() == -511);
    printf("DTLN SDS scheduler PASS, worker reuse, empty clip, runtime and SDS stop, signal processing, LCD and speaker routing\n");
}
