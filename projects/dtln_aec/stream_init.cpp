// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "app_config.hpp"
#include "IdentifiedNode.hpp"
#include "scheduler_app.h"
#include "stream_event_queue.hpp"
#include "stream_init.hpp"
#include "stream_runtime_init.hpp"
#include "nodes_impl/SpeakerOutput.hpp"
#include "nodes_impl/WaveformCapture.hpp"
#include <atomic>
#include <new>

namespace {
class ReplayEventQueue final : public CMSISEventQueue {
public:
    ReplayEventQueue()
        : CMSISEventQueue(CMSISSTREAM_EVT_LOW_PRIORITY, CMSISSTREAM_EVT_NORMAL_PRIORITY,
                          CMSISSTREAM_EVT_HIGH_PRIORITY) {}

    ~ReplayEventQueue() override {
        // The SDK queue destructor frees its message buffers. Release its RTX wake flag too.
        if (cg_eventEvent) osEventFlagsDelete(cg_eventEvent);
    }

    bool ready() const {
        return cg_eventEvent && std::all_of(std::begin(queue), std::end(queue),
                                            [](const auto* buffer) { return buffer != nullptr; });
    }
};

constexpr auto hop = DTLN_AUDIO_HOP_SAMPLES;
using CaptureNode = WaveformCapture<float, hop, float, hop>;
using SpeakerNode = SpeakerOutput<float, hop>;

ReplayEventQueue* eventQueue;
osSemaphoreId_t lcdDrained;
stream_execution_context_t context;
StreamStats* clipStats;
std::atomic<bool> finished;
std::atomic<int> runtimeError;
int schedulerResult;
bool lcdDrainFailed;

bool handleEvent(int, void*, Event&& event)
{
    if (event.event_id == kValue) return osSemaphoreRelease(lcdDrained) == osOK;
    if (event.event_id == kError && event.wellFormed<int32_t, int32_t, int32_t>()) {
        event.apply<int32_t, int32_t, int32_t>([](int32_t, int32_t status, int32_t) {
            runtimeError.store(status);
        });
        return true;
    }
    return event.event_id == kStopGraph;
}

uint32_t runScheduler(int* error)
{
    const uint32_t iterations = scheduler_app(error);
    if (*error == CG_STOP_SCHEDULER && !dtlnPlaybackStopping()) {
        auto* waveform = static_cast<CaptureNode*>(get_scheduler_app_node(STREAM_APP_WAVEFORM_ID)->obj);
        auto* speaker = static_cast<SpeakerNode*>(get_scheduler_app_node(STREAM_APP_SPEAKER_ID)->obj);
        // The marker follows the final snapshot at the same priority. Give scanout time to drain
        // before returning to the runtime, which pauses the event queue once the scheduler exits.
        const bool queued = waveform->finish() && EventOutput(eventQueue).sendAsyncToApp(
            CG_UNIDENTIFIED_NODE, kLowPriority, kValue, uint32_t{0});
        speaker->finish();
        const auto timeout = (uint64_t{550} * osKernelGetTickFreq() + 999) / 1000;
        lcdDrainFailed = !queued || osSemaphoreAcquire(lcdDrained, static_cast<uint32_t>(timeout)) != osOK;
    }
    schedulerResult = *error;
    finished.store(true, std::memory_order_release);
    return iterations;
}

void releaseResources()
{
    EventQueue::setHandler(nullptr, nullptr);
    delete eventQueue;
    eventQueue = nullptr;
    if (lcdDrained) osSemaphoreDelete(lcdDrained);
    lcdDrained = nullptr;
    stream_free_memory();
}
}

int dtln_stream_start(const HardwareParams& hardware)
{
    if (stream_init_memory() != 0) return CG_MEMORY_ALLOCATION_FAILURE;
    eventQueue = new (std::nothrow) ReplayEventQueue;
    lcdDrained = osSemaphoreNew(1, 0, nullptr);
    if (!eventQueue || !eventQueue->ready() || !lcdDrained) {
        releaseResources();
        return CG_MEMORY_ALLOCATION_FAILURE;
    }

    app_params_set_hardware(&hardware);
    const int error = init_scheduler_app(eventQueue, &appParams);
    if (error != CG_SUCCESS) {
        free_scheduler_app();
        releaseResources();
        return error;
    }

    reset_fifos_scheduler_app(1);
    for (int id = 0; id < STREAM_APP_NB_IDENTIFIED_NODES; ++id) {
        auto* node = get_scheduler_app_node(id);
        if (node->state_reset_intf) node->state_reset_intf->reset(node->obj);
    }

    clipStats = hardware.stats;
    finished.store(false);
    runtimeError.store(CG_SUCCESS);
    schedulerResult = CG_SUCCESS;
    lcdDrainFailed = false;
    EventQueue::setHandler(nullptr, handleEvent);
    context = {runScheduler, reset_fifos_scheduler_app, nullptr, nullptr, nullptr,
               eventQueue, STREAM_APP_NB_IDENTIFIED_NODES, STREAM_APP_SCHED_LEN};
    if (!stream_start_threads(&context)) {
        free_scheduler_app();
        releaseResources();
        return CG_OS_ERROR;
    }
    return CG_SUCCESS;
}

bool dtln_stream_finished()
{
    return finished.load(std::memory_order_acquire);
}

int dtln_stream_finish()
{
    stream_stop_threads();
    if (lcdDrainFailed && !clipStats->lcd_error) clipStats->lcd_error = CG_OS_ERROR;
    const int error = runtimeError.load();
    free_scheduler_app();
    releaseResources();
    return error != CG_SUCCESS ? error : schedulerResult;
}
