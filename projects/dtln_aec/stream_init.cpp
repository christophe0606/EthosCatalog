// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "cmsis_os2.h"
#include "stream_platform_config.hpp"
#include "dtln_app_config.hpp"
#include "IdentifiedNode.hpp"
#include "SdsSink.hpp"
#include "SdsSource.hpp"
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
using ReplayNode = SdsSource<float, 2 * hop>;
using RecordNode = SdsSink<float, hop>;

ReplayEventQueue* eventQueue;
osSemaphoreId_t lcdDrained;
osSemaphoreId_t clipReady;
stream_execution_context_t context;
StreamStats* clipStats;
std::atomic<bool> finished;
std::atomic<int> runtimeError;
int schedulerResult;
bool lcdDrainFailed;
bool reachedEof;

uint32_t timeoutTicks(uint32_t milliseconds)
{
    return (uint64_t{milliseconds} * osKernelGetTickFreq() + 999) / 1000;
}

void* getNodeById(int32_t id)
{
    return get_scheduler_app_node(id);
}

void switchNodes(const stream_execution_context_t* graph, int (*ContextSwitchInterface::*operation)(void*))
{
    for (uint32_t id = 0; id < graph->nb_identified_nodes; ++id) {
        auto* node = static_cast<CStreamNode*>(graph->get_node_by_id(id));
        if (node && node->obj && node->context_switch_intf) {
            const int status = (node->context_switch_intf->*operation)(node->obj);
            if (status != CG_SUCCESS) runtimeError.store(status);
        }
    }
}

void pause_scheduler_app(const stream_execution_context_t* graph)
{
    switchNodes(graph, &ContextSwitchInterface::pause);
}

void resume_scheduler_app(const stream_execution_context_t* graph)
{
    switchNodes(graph, &ContextSwitchInterface::resume);
}

void prepareClip()
{
    finished.store(false);
    runtimeError.store(CG_SUCCESS);
    lcdDrainFailed = false;
    reachedEof = false;
    auto* source = static_cast<ReplayNode*>(get_scheduler_app_node(STREAM_APP_SDS_SOURCE_ID)->obj);
    auto* sink = static_cast<RecordNode*>(get_scheduler_app_node(STREAM_APP_SDS_SINK_ID)->obj);
    source->bindStream(dtlnReplayParams.stream);
    sink->bindStream(dtlnRecordParams.stream);
    for (int id = 0; id < STREAM_APP_NB_IDENTIFIED_NODES; ++id) {
        auto* node = get_scheduler_app_node(id);
        if (node && node->obj && node->state_reset_intf) node->state_reset_intf->reset(node->obj);
    }
}

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
    // Resume wakes dataflow first. Hold the first firing until the event thread also
    // acknowledges resume, so even an empty input cannot pause it mid-handshake.
    if (osSemaphoreAcquire(clipReady, timeoutTicks(1000)) != osOK) {
        *error = CG_PAUSED_SCHEDULER;
        schedulerResult = CG_RESUME_FAILURE;
        finished.store(true, std::memory_order_release);
        return 0;
    }
    const uint32_t iterations = scheduler_app(error);
    const auto* source = static_cast<ReplayNode*>(get_scheduler_app_node(STREAM_APP_SDS_SOURCE_ID)->obj);
    reachedEof = *error == CG_STOP_SCHEDULER && source->lastSdsStatus() == SDS_EOS && !dtlnPlaybackStopping();
    if (reachedEof) {
        auto* waveform = static_cast<CaptureNode*>(get_scheduler_app_node(STREAM_APP_WAVEFORM_ID)->obj);
        auto* speaker = static_cast<SpeakerNode*>(get_scheduler_app_node(STREAM_APP_SPEAKER_ID)->obj);
        // The marker follows the final snapshot at the same priority. Give scanout time to drain
        // before returning to the runtime, which pauses the event queue once the scheduler exits.
        const bool queued = waveform->finish() && EventOutput(eventQueue).sendAsyncToApp(
            CG_UNIDENTIFIED_NODE, kLowPriority, kValue, uint32_t{0});
        speaker->finish();
        lcdDrainFailed = !queued || osSemaphoreAcquire(lcdDrained, timeoutTicks(550)) != osOK;
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
    if (clipReady) osSemaphoreDelete(clipReady);
    clipReady = nullptr;
    stream_free_memory();
}
}

int dtln_stream_start(const HardwareParams& hardware)
{
    if (eventQueue) {
        prepareClip();
        if (!stream_resume_scheduler(&context) || runtimeError.load() != CG_SUCCESS) {
            const int status = runtimeError.load();
            dtln_stream_shutdown();
            return status == CG_SUCCESS ? CG_RESUME_FAILURE : status;
        }
        osSemaphoreRelease(clipReady);
        return CG_SUCCESS;
    }

    if (stream_init_memory() != 0) return CG_MEMORY_ALLOCATION_FAILURE;
    eventQueue = new (std::nothrow) ReplayEventQueue;
    lcdDrained = osSemaphoreNew(1, 0, nullptr);
    clipReady = osSemaphoreNew(1, 0, nullptr);
    if (!eventQueue || !eventQueue->ready() || !lcdDrained || !clipReady) {
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

    EventQueue::setHandler(nullptr, handleEvent);
    context = {runScheduler, reset_fifos_scheduler_app, pause_scheduler_app, resume_scheduler_app, getNodeById,
               eventQueue, STREAM_APP_NB_IDENTIFIED_NODES, STREAM_APP_SCHED_LEN};
    clipStats = hardware.stats;
    prepareClip();
    reset_fifos_scheduler_app(1);
    eventQueue->resume();
    resume_scheduler_app(&context);
    const int status = runtimeError.load();
    if (status != CG_SUCCESS) {
        free_scheduler_app();
        releaseResources();
        return status;
    }
    osSemaphoreRelease(clipReady);
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
    bool pauseTimedOut = false;
    if (reachedEof && !lcdDrainFailed) {
        // The STOP path already pauses both workers. The runtime consumes EVENT_PAUSED_EVENT
        // before posting this acknowledgement. Clear any events left after the final frame.
        const uint32_t flags = osEventFlagsWait(cg_streamReplyEvent, STREAM_PAUSED_EVENT,
                                                osFlagsWaitAny, timeoutTicks(1000));
        pauseTimedOut = (flags & osFlagsError) || !(flags & STREAM_PAUSED_EVENT);
        if (!pauseTimedOut && runtimeError.load() == CG_SUCCESS) {
            eventQueue->clear();
            return schedulerResult;
        }
    }
    dtln_stream_shutdown();
    if (lcdDrainFailed && !clipStats->lcd_error) clipStats->lcd_error = CG_OS_ERROR;
    const int error = runtimeError.load();
    if (error != CG_SUCCESS) return error;
    return pauseTimedOut ? CG_OS_ERROR : schedulerResult;
}

void dtln_stream_shutdown()
{
    if (!eventQueue) return;
    stream_stop_threads();
    auto* speaker = static_cast<SpeakerNode*>(get_scheduler_app_node(STREAM_APP_SPEAKER_ID)->obj);
    speaker->stop();
    free_scheduler_app();
    releaseResources();
}
