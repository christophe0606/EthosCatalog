// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "encodec_app_config.hpp"
#include "codec_runtime.hpp"
#include "sdsio_client.h"

#include <atomic>
#include <cstdio>

SdsSourceParams encodecReplayParams;
SdsSinkParams encodecRecordParams;

namespace {
constexpr std::size_t bufferSize = 16384;
// The SDS host sends a heartbeat every 100 ms. The client clears ALIVE
// after ten exchanges without one, so leave room for USB scheduling jitter.
constexpr uint32_t controlPollsPerSecond = 40;
constexpr uint32_t playbackFlags = SDS_FLAG_START | SDS_FLAG_PLAYBACK;
constexpr uint32_t runningFlags = SDS_FLAG_ALIVE | playbackFlags;
constexpr uint32_t stopFlags = SDS_FLAG_RESET | SDS_FLAG_TERMINATE;
static_assert(ENCODEC_SAMPLE_RATE == encodec::sampleRate && ENCODEC_BLOCK_SAMPLES == encodec::blockSamples);
static_assert(encodec::blockSamples * sizeof(float) + 2 * sizeof(uint32_t) < bufferSize);
alignas(32) uint8_t replayBuffer[bufferSize], recordBuffer[bufferSize];
SdsPlaybackTiming playback;
std::atomic<bool> stopping;

int playClip(const HardwareParams& hardware, uint32_t pollTicks)
{
    sdsState = SDS_STATE_START;
    playback = {};
    stopping.store(false, std::memory_order_relaxed);
    encodecDemoStats = {};
    encodecDemoStats.lcd_error = hardware.lcd->status;
    encodecDemoStats.timer_hz = osKernelGetSysTimerFreq();
    encodecDemoStats.codec_budget_ticks = uint64_t{encodecDemoStats.timer_hz} *
                                         encodec::blockSamples / encodec::sampleRate;

    const auto replay = sdsOpen("Encodec_In", sdsModeRead, replayBuffer, sizeof(replayBuffer));
    const auto record = replay ? sdsOpen("Encodec_Out", sdsModeWrite, recordBuffer, sizeof(recordBuffer)) : nullptr;
    int result = CG_BUFFER_ERROR;
    if (replay && record) {
        encodecReplayParams = {replay, &playback, encodec::sampleRate, encodec::blockSamples};
        encodecRecordParams = {record, &playback};
        sdsState = SDS_STATE_ACTIVE;
        result = encodec_stream_start(hardware);
        if (result == CG_SUCCESS) {
            const uint32_t heartbeatGrace = osKernelGetTickFreq();
            uint32_t lastAlive = osKernelGetTickCount();
            while (!encodec_stream_finished()) {
                const int exchange = sdsExchange();
                if (exchange == SDS_ERROR_IO) encodecDemoStats.transport_error = exchange;
                const uint32_t flags = sdsFlags;
                const uint32_t now = osKernelGetTickCount();
                if (flags & SDS_FLAG_ALIVE) {
                    lastAlive = now;
                } else if (!encodecDemoStats.transport_error && now - lastAlive >= heartbeatGrace) {
                    encodecDemoStats.transport_error = SDS_ERROR_TIMEOUT;
                }
                if (!encodecPlaybackStopping() && (encodecDemoStats.transport_error ||
                    (flags & playbackFlags) != playbackFlags || (flags & stopFlags))) {
                    encodecDemoStats.control_stop_flags = flags;
                    stopping.store(true, std::memory_order_relaxed);
                }
                osDelay(pollTicks);
            }
            result = encodec_stream_finish();
            if (result == CG_STOP_SCHEDULER &&
                (encodecDemoStats.replay_status == SDS_EOS || encodecPlaybackStopping())) {
                result = CG_SUCCESS;
            }
        }
    } else {
        encodecDemoStats.transport_error = SDS_ERROR_IO;
    }

    sdsState = SDS_STATE_STOP_REQ;
    const int recordClose = record ? sdsClose(record) : SDS_OK;
    const int replayClose = replay ? sdsClose(replay) : SDS_OK;
    encodecReplayParams = {};
    encodecRecordParams = {};
    const int closeError = recordClose != SDS_OK ? recordClose : replayClose;
    if (closeError) encodecDemoStats.transport_error = closeError;
    if (encodecDemoStats.transport_error && !result) result = CG_BUFFER_ERROR;
    encodecDemoStats.stream_error = result;

    std::printf("EnCodec SDS result=%d close=%d blocks=%lu codec_error=%ld lcd_error=%ld\n",
                result, closeError, static_cast<unsigned long>(encodecDemoStats.completed_blocks),
                static_cast<long>(encodecDemoStats.codec_error), static_cast<long>(encodecDemoStats.lcd_error));
    std::printf("EnCodec timing encoder_ticks_max=%lu decoder_ticks_max=%lu codec_ticks_max=%lu "
                "budget_ticks=%lu encoder_deadline_misses=%lu decoder_deadline_misses=%lu codec_deadline_misses=%lu "
                "replay_deadline_misses=%lu lcd_frames=%lu waveform_drops=%lu\n",
                static_cast<unsigned long>(encodecDemoStats.encoder_ticks_max),
                static_cast<unsigned long>(encodecDemoStats.decoder_ticks_max),
                static_cast<unsigned long>(encodecDemoStats.codec_ticks_max),
                static_cast<unsigned long>(encodecDemoStats.codec_budget_ticks),
                static_cast<unsigned long>(encodecDemoStats.encoder_deadline_misses),
                static_cast<unsigned long>(encodecDemoStats.decoder_deadline_misses),
                static_cast<unsigned long>(encodecDemoStats.codec_deadline_misses),
                static_cast<unsigned long>(encodecDemoStats.replay_deadline_misses),
                static_cast<unsigned long>(encodecDemoStats.lcd_frames),
                static_cast<unsigned long>(encodecDemoStats.waveform_drops));
    if (closeError != SDS_OK) return closeError;

    sdsState = SDS_STATE_STOP_DONE;
    sdsFlagsModify(0, SDS_FLAG_START);
    return SDS_OK;
}
}

bool encodecPlaybackStopping()
{
    return stopping.load(std::memory_order_relaxed);
}

void encodecRecordCompletedBlock()
{
    ++encodecDemoStats.completed_blocks;
    if (sds_node_detail::ticksUntil(playback.nextReleaseTick) < 0) {
        ++encodecDemoStats.replay_deadline_misses;
    }
}

extern "C" int app_main(void)
{
    static HardwareParams hardware;
    // Configure display power before staging codec weights into SRAM.
    hardware_params_init(&hardware);

    const auto codec = encodec::initializeCodec();
    encodecDemoStats.codec_error = static_cast<int32_t>(codec);
    if (codec != executorch::runtime::Error::Ok) return static_cast<int>(codec);

    const uint32_t ticksPerSecond = osKernelGetTickFreq();
    int result;
    while ((result = sdsInit(nullptr)) == SDS_ERROR_IO) {
        // SDS can leave the USB client open after an enumeration timeout.
        result = sdsioClientUninit();
        if (result != SDS_OK) {
            encodecDemoStats.transport_error = result;
            return result;
        }
        osDelay((ticksPerSecond + 3) / 4);
    }
    if (result != SDS_OK) {
        encodecDemoStats.transport_error = result;
        return result;
    }

    const uint32_t pollTicks = (ticksPerSecond + controlPollsPerSecond - 1) / controlPollsPerSecond;
    for (;;) {
        sdsExchange();
        const uint32_t flags = sdsFlags;
        if (flags & SDS_FLAG_RESET) hardware_params_reset_board();
        if (flags & SDS_FLAG_TERMINATE) sdsFlagsModify(0, SDS_FLAG_TERMINATE | SDS_FLAG_START);

        if (sdsState == SDS_STATE_STOP_DONE || !(flags & SDS_FLAG_ALIVE)) {
            sdsState = SDS_STATE_INACTIVE;
        } else {
            sdsState = SDS_STATE_CONNECTED;
            if ((flags & runningFlags) == runningFlags && !(flags & stopFlags)) {
                result = playClip(hardware, pollTicks);
                if (result != SDS_OK) return result;
            } else if (flags & SDS_FLAG_START) {
                sdsFlagsModify(0, SDS_FLAG_START);
            }
        }
        osDelay(pollTicks);
    }
}
