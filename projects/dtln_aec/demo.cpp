// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "dtln_app_config.hpp"
#include "cmsis_os2.h"
#include "sdsio_client.h"
#include "stream_init.hpp"
#include <algorithm>
#include <atomic>
#include <cstdio>

SdsSourceParams dtlnReplayParams;
SdsSinkParams dtlnRecordParams;

namespace {
constexpr uint32_t bufferSize = 16384;
alignas(32) uint8_t replayBuffer[bufferSize], recordBuffer[bufferSize];
SdsPlaybackTiming playback;
uint32_t hopBudgetTicks;
std::atomic<bool> stopping{false};
std::atomic<bool> transportError{false};

int playClip(HardwareParams& hardware)
{
    sdsState = SDS_STATE_START;
    playback = {};
    stopping.store(false);
    transportError.store(false);
    *hardware.stats = {};
    hopBudgetTicks = uint64_t{osKernelGetSysTimerFreq()} * DTLN_AUDIO_HOP_SAMPLES / DTLN_AUDIO_SAMPLE_RATE;

    auto replay = sdsOpen("Dtln_In", sdsModeRead, replayBuffer, sizeof(replayBuffer));
    auto record = replay ? sdsOpen("Dtln_Out", sdsModeWrite, recordBuffer, sizeof(recordBuffer)) : nullptr;
    int result = CG_BUFFER_ERROR;
    if (replay && record) {
        hardware_params_init_speaker(&hardware);
        dtlnReplayParams = {replay, &playback, DTLN_AUDIO_SAMPLE_RATE, DTLN_AUDIO_HOP_SAMPLES};
        dtlnRecordParams = {record, &playback};
        sdsState = SDS_STATE_ACTIVE;

        result = dtln_stream_start(hardware);
        if (result == CG_SUCCESS) {
            const uint32_t pollTicks = (osKernelGetTickFreq() + 9) / 10;
            while (!dtln_stream_finished()) {
                if (sdsExchange() == SDS_ERROR_IO) transportError.store(true);
                constexpr uint32_t running = SDS_FLAG_ALIVE | SDS_FLAG_START;
                if ((sdsFlags & running) != running ||
                    (sdsFlags & (SDS_FLAG_RESET | SDS_FLAG_TERMINATE))) {
                    stopping.store(true);
                }
                osDelay(pollTicks);
            }
            result = dtln_stream_finish();
            if (result == CG_STOP_SCHEDULER) result = CG_SUCCESS;
        }
        hardware_params_uninit_speaker(&hardware);
        if (hardware.stats->speaker_error) {
            printf("DTLN speaker error=%ld\n", static_cast<long>(hardware.stats->speaker_error));
        }
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
           static_cast<unsigned long>(playback.timeslot));
    printf("DTLN SDS timing max_hop_ticks=%lu budget_ticks=%lu deadline_misses=%lu lcd_frames=%lu lcd_error=%ld\n",
           static_cast<unsigned long>(hardware.stats->hop_ticks_max),
           static_cast<unsigned long>(hopBudgetTicks),
           static_cast<unsigned long>(hardware.stats->deadline_misses),
           static_cast<unsigned long>(hardware.stats->lcd_frames),
           static_cast<long>(hardware.stats->lcd_error));
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
    ticks -= playback.pacingTimerCounts;
    stream_stats.hop_ticks_max = std::max(stream_stats.hop_ticks_max, ticks);
    if (ticks > hopBudgetTicks) ++stream_stats.deadline_misses;
}

extern "C" int dtln_demo_start(void)
{
    static HardwareParams hardware;
    int error = hardware_params_init(&hardware);
    if (error) return error;
    const uint32_t ticksPerSecond = osKernelGetTickFreq();
    while ((error = sdsInit(nullptr)) == SDS_ERROR_IO) {
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
        if (sdsFlags & SDS_FLAG_RESET) {
            dtln_stream_shutdown();
            hardware_params_reset_board();
        }
        if (sdsFlags & SDS_FLAG_TERMINATE) {
            dtln_stream_shutdown();
            sdsFlagsModify(0, SDS_FLAG_TERMINATE | SDS_FLAG_START);
        }

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
                              error == CG_MEMORY_ALLOCATION_FAILURE)) {
                    dtln_stream_shutdown();
                    return error;
                }
            } else if (sdsFlags & SDS_FLAG_START) {
                sdsFlagsModify(0, SDS_FLAG_START);
            }
        }
        osDelay(pollTicks);
    }
}
