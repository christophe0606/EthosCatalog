// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "RTE_Components.h"
#include CMSIS_device_header
#include "dtln_app_config.hpp"
#include "cmsis_os2.h"
#include "sdsio_client.h"
#include "stream_init.hpp"
#include "waveform_display.hpp"
#include "../../board/DevKit-E8/audio_output.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>

SdsSourceParams dtlnReplayParams;
SdsSinkParams dtlnRecordParams;

namespace {
constexpr uint32_t bufferSize = 16384;
constexpr uint32_t hopsPerSecond = DTLN_AUDIO_SAMPLE_RATE / DTLN_AUDIO_HOP_SAMPLES;
constexpr uint32_t hopPeriodMs = 1000 / hopsPerSecond;
static_assert(DTLN_AUDIO_SAMPLE_RATE % DTLN_AUDIO_HOP_SAMPLES == 0 && 1000 % hopsPerSecond == 0);
static_assert(DTLN_AUDIO_SAMPLE_RATE == DEVKIT_E8_AUDIO_RATE);
static_assert(DEVKIT_E8_AUDIO_FRAMES % DTLN_AUDIO_HOP_SAMPLES == 0);
// Allow the full speaker queue and the hardware FIFO to drain at clip end.
constexpr unsigned speakerDrainTimeoutMs = 200;
alignas(32) uint8_t replayBuffer[bufferSize], recordBuffer[bufferSize];
alignas(8) uint8_t graphStack[24576];
uint32_t timeslot;
uint32_t hopBudgetTicks;
uint32_t hopPeriodTicks;
uint32_t playbackTick;
uint32_t pacingWaitTicks;
bool playbackClockStarted;
std::atomic<bool> stopping{false};
std::atomic<bool> transportError{false};
std::atomic<bool> finished{false};
int graphResult;

struct SpeakerPlayback {
    int16_t block[DEVKIT_E8_AUDIO_FRAMES];
    unsigned pending = 0;
    int error = 0;

    void writeBlock()
    {
        error = devkit_e8_audio_write(block, DEVKIT_E8_AUDIO_FRAMES);
        pending = 0;
    }

    void append(const float* samples)
    {
        if (error) return;
        for (unsigned i = 0; i < DTLN_AUDIO_HOP_SAMPLES; ++i) {
            float sample = samples[i];
            // Check the representation so this also works with finite-math optimizations.
            uint32_t bits;
            std::memcpy(&bits, &sample, sizeof(bits));
            if ((bits & 0x7f800000U) == 0x7f800000U) sample = 0.0f;
            sample = std::clamp(sample, -1.0f, 32767.0f / 32768.0f);
            block[pending++] = static_cast<int16_t>(sample * 32768.0f);
        }
        if (pending == DEVKIT_E8_AUDIO_FRAMES) writeBlock();
    }

    void finish()
    {
        if (error) return;
        if (pending) {
            std::fill(block + pending, block + DEVKIT_E8_AUDIO_FRAMES, int16_t{0});
            writeBlock();
        }
        if (!error) error = devkit_e8_audio_drain(speakerDrainTimeoutMs);
    }
};

void observeOutput(void* context, const void* record, uint32_t bytes)
{
    if (bytes == DTLN_AUDIO_HOP_SAMPLES * sizeof(float)) {
        static_cast<SpeakerPlayback*>(context)->append(static_cast<const float*>(record));
    }
    const auto waveform = dtln::waveform::outputObserver();
    waveform.observe(waveform.context, record, bytes);
}

void sdsEvent(sdsId_t, uint32_t events)
{
    if (events & SDS_EVENT_ERROR_IO) transportError.store(true);
}

bool waitForSds(void*, int32_t)
{
    return osDelay(1) == osOK && !dtlnPlaybackStopping();
}

cg_status pacePlayback(uint32_t inputTimeslot)
{
    if (dtlnPlaybackStopping()) return CG_STOP_SCHEDULER;
    const uint32_t now = osKernelGetTickCount();
    pacingWaitTicks = 0;
    if (!playbackClockStarted) {
        playbackTick = now;
        playbackClockStarted = true;
    } else {
        if (inputTimeslot - timeslot != hopPeriodMs) return CG_BUFFER_ERROR;
        playbackTick += hopPeriodTicks;
        if (static_cast<int32_t>(playbackTick - now) > 0) {
            const uint32_t waitStart = osKernelGetSysTimerCount();
            if (osDelayUntil(playbackTick) != osOK &&
                static_cast<int32_t>(playbackTick - osKernelGetTickCount()) > 0) return CG_OS_ERROR;
            pacingWaitTicks = osKernelGetSysTimerCount() - waitStart;
        } else {
            // Resume from the current time after a late USB record, without a catch-up burst.
            playbackTick = now;
        }
    }
    return dtlnPlaybackStopping() ? CG_STOP_SCHEDULER : CG_SUCCESS;
}

uint32_t recordTimeslot(void*)
{
    return timeslot;
}

void runGraph(void* hardware)
{
    graphResult = stream_run(*static_cast<HardwareParams*>(hardware));
    dtln::waveform::finish();
    finished.store(true, std::memory_order_release);
}

int playClip(HardwareParams& hardware)
{
    sdsState = SDS_STATE_START;
    stopping.store(false);
    transportError.store(false);
    finished.store(false);
    timeslot = 0;
    playbackClockStarted = false;
    *hardware.stats = {};
    dtln::waveform::reset();
    const auto lcdFramesAtStart = dtln::waveform::framesPresented();
    hopBudgetTicks = osKernelGetSysTimerFreq() / hopsPerSecond;
    hopPeriodTicks = osKernelGetTickFreq() / hopsPerSecond;

    auto replay = sdsOpen("Dtln_In", sdsModeRead, replayBuffer, sizeof(replayBuffer));
    auto record = replay ? sdsOpen("Dtln_Out", sdsModeWrite, recordBuffer, sizeof(recordBuffer)) : nullptr;
    int result = CG_BUFFER_ERROR;
    if (replay && record) {
        SpeakerPlayback speaker;
        speaker.error = devkit_e8_audio_init();
        constexpr SdsRetry retry{waitForSds, nullptr, 1500};
        dtlnReplayParams = {replay, &timeslot, retry, dtln::waveform::microphoneObserver(), pacePlayback};
        dtlnRecordParams = {record, recordTimeslot, nullptr, retry, {observeOutput, &speaker}};
        sdsState = SDS_STATE_ACTIVE;

        osThreadAttr_t attr{};
        attr.name = "DTLN SDS graph";
        attr.attr_bits = osThreadJoinable;
        attr.stack_mem = graphStack;
        attr.stack_size = sizeof(graphStack);
        auto graph = osThreadNew(runGraph, &hardware, &attr);
        if (!graph) {
            result = CG_OS_ERROR;
        } else {
            const uint32_t pollTicks = (osKernelGetTickFreq() + 9) / 10;
            while (!finished.load(std::memory_order_acquire)) {
                sdsExchange();
                constexpr uint32_t running = SDS_FLAG_ALIVE | SDS_FLAG_START;
                if ((sdsFlags & running) != running ||
                    (sdsFlags & (SDS_FLAG_RESET | SDS_FLAG_TERMINATE))) {
                    stopping.store(true);
                }
                osDelay(pollTicks);
            }
            if (osThreadJoin(graph) != osOK) {
                result = CG_OS_ERROR;
            } else {
                result = graphResult == CG_STOP_SCHEDULER ? CG_SUCCESS : graphResult;
            }
        }
        if (result == CG_SUCCESS && !dtlnPlaybackStopping()) speaker.finish();
        devkit_e8_audio_stop();
        if (speaker.error) printf("DTLN speaker error=%d\n", speaker.error);
    }

    sdsState = SDS_STATE_STOP_REQ;
    const int recordClose = record ? sdsClose(record) : SDS_OK;
    const int replayClose = replay ? sdsClose(replay) : SDS_OK;
    dtlnReplayParams = {};
    dtlnRecordParams = {};
    if (transportError.load() && !result) result = CG_BUFFER_ERROR;
    const int closeError = recordClose != SDS_OK ? recordClose : replayClose;
    printf("DTLN SDS playback result=%d close=%d hops=%lu last_timeslot=%lu\n",
           result, closeError, static_cast<unsigned long>(hardware.stats->stage2_calls),
           static_cast<unsigned long>(timeslot));
    printf("DTLN SDS timing max_hop_ticks=%lu budget_ticks=%lu deadline_misses=%lu lcd_frames=%lu lcd_error=%ld\n",
           static_cast<unsigned long>(hardware.stats->hop_ticks_max),
           static_cast<unsigned long>(hopBudgetTicks),
           static_cast<unsigned long>(hardware.stats->deadline_misses),
           static_cast<unsigned long>(dtln::waveform::framesPresented() - lcdFramesAtStart),
           static_cast<long>(dtln::waveform::displayError()));
    if (closeError != SDS_OK) return closeError;
    sdsState = SDS_STATE_STOP_DONE;
    sdsFlagsModify(0, SDS_FLAG_START);
    return result;
}
} // namespace

bool dtlnPlaybackStopping()
{
    return stopping.load() || transportError.load();
}

void dtlnRecordHopTicks(uint32_t ticks)
{
    ticks -= pacingWaitTicks;
    stream_stats.hop_ticks_max = std::max(stream_stats.hop_ticks_max, ticks);
    if (ticks > hopBudgetTicks) ++stream_stats.deadline_misses;
}

extern "C" int dtln_demo_start(void)
{
    static HardwareParams hardware;
    int error = hardware_params_init(&hardware);
    if (error) return error;
    dtln::waveform::start();
    const uint32_t ticksPerSecond = osKernelGetTickFreq();
    while ((error = sdsInit(sdsEvent)) == SDS_ERROR_IO) {
        // SDS 3.1 leaves the USB client open when enumeration times out
        // before SDSIO is marked initialized. Disconnect before trying again.
        error = sdsioClientUninit();
        if (error != SDS_OK) return error;
        osDelay((ticksPerSecond + 3) / 4);
    }
    if (error != SDS_OK) return error;

    const uint32_t pollTicks = (ticksPerSecond + 9) / 10;
    for (;;) {
        sdsExchange();
        if (sdsFlags & SDS_FLAG_RESET) NVIC_SystemReset();
        if (sdsFlags & SDS_FLAG_TERMINATE) sdsFlagsModify(0, SDS_FLAG_TERMINATE | SDS_FLAG_START);

        if (sdsState == SDS_STATE_STOP_DONE) {
            sdsState = SDS_STATE_INACTIVE;
        } else if (!(sdsFlags & SDS_FLAG_ALIVE)) {
            sdsState = SDS_STATE_INACTIVE;
        } else {
            sdsState = SDS_STATE_CONNECTED;
            if ((sdsFlags & (SDS_FLAG_START | SDS_FLAG_PLAYBACK)) ==
                (SDS_FLAG_START | SDS_FLAG_PLAYBACK)) {
                error = playClip(hardware);
                if (error && (sdsState != SDS_STATE_STOP_DONE || error == CG_INIT_FAILURE ||
                              error == CG_MEMORY_ALLOCATION_FAILURE)) return error;
            } else if (sdsFlags & SDS_FLAG_START) {
                sdsFlagsModify(0, SDS_FLAG_START);
            }
        }
        osDelay(pollTicks);
    }
}
