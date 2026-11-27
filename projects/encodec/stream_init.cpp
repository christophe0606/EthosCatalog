// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "encodec_app_config.hpp"
#include "SdsSink.hpp"
#include "SdsSource.hpp"
#include "codec_runtime.hpp"
#include "nodes_impl/WaveformCapture.hpp"
#include "scheduler_app.h"
#include "stream_event_queue.hpp"
#include "stream_runtime_init.hpp"

#include <algorithm>
#include <atomic>
#include <iterator>
#include <new>

namespace {
class ReplayEventQueue final : public CMSISEventQueue {
public:
    ReplayEventQueue()
        : CMSISEventQueue(CMSISSTREAM_EVT_LOW_PRIORITY, CMSISSTREAM_EVT_NORMAL_PRIORITY,
                          CMSISSTREAM_EVT_HIGH_PRIORITY) {}

    ~ReplayEventQueue() override {
        if (cg_eventEvent) osEventFlagsDelete(cg_eventEvent);
    }

    bool ready() const {
        return cg_eventEvent && std::all_of(std::begin(queue), std::end(queue),
                                            [](const auto* buffer) { return buffer != nullptr; });
    }
};

using ReplayNode = SdsSource<float, ENCODEC_BLOCK_SAMPLES>;
using RecordNode = SdsSink<float, ENCODEC_BLOCK_SAMPLES>;
using WaveformNode = WaveformCapture<float, ENCODEC_BLOCK_SAMPLES>;

ReplayEventQueue* eventQueue;
osSemaphoreId_t lcdDrained;
stream_execution_context_t context;
std::atomic<bool> finished;
int schedulerResult;

void* getNodeById(int32_t id)
{
    return get_scheduler_app_node(id);
}

bool handleEvent(int, void*, Event&& event)
{
    if (event.event_id == kValue) return osSemaphoreRelease(lcdDrained) == osOK;
    return event.event_id == kError || event.event_id == kStopGraph;
}

void drainWaveform()
{
    auto* waveform = static_cast<WaveformNode*>(get_scheduler_app_node(STREAM_APP_WAVEFORM_ID)->obj);
    const bool queued = waveform->finish() && EventOutput(eventQueue).sendAsyncToApp(
        CG_UNIDENTIFIED_NODE, kLowPriority, kValue, uint32_t{0});
    const uint32_t timeout = (uint64_t{osKernelGetTickFreq()} * 550 + 999) / 1000;
    // The marker follows the last waveform event at the same priority.
    if (!queued || osSemaphoreAcquire(lcdDrained, timeout) != osOK) {
        encodecDemoStats.waveform_drain_error = queued ? CG_OS_ERROR : CG_EVENT_QUEUE_FULL;
    }
}

uint32_t runScheduler(int* error)
{
    const uint32_t iterations = scheduler_app(error);
    const auto* source = static_cast<ReplayNode*>(get_scheduler_app_node(STREAM_APP_SDS_SOURCE_ID)->obj);
    const auto* sink = static_cast<RecordNode*>(get_scheduler_app_node(STREAM_APP_SDS_SINK_ID)->obj);
    encodecDemoStats.replay_status = source->lastSdsStatus();
    encodecDemoStats.record_status = sink->lastSdsStatus();
    if (*error == CG_BUFFER_UNDERFLOW || *error == CG_BUFFER_OVERFLOW) {
        ++encodecDemoStats.replay_deadline_misses;
    }
    if (*error == CG_STOP_SCHEDULER && source->lastSdsStatus() == SDS_EOS && !encodecPlaybackStopping()) {
        drainWaveform();
    }
    schedulerResult = *error;
    finished.store(true, std::memory_order_release);
    return iterations;
}

void freeGraph()
{
    EventQueue::setHandler(nullptr, nullptr);
    if (eventQueue) eventQueue->clear();
    free_scheduler_app();
    delete eventQueue;
    eventQueue = nullptr;
    if (lcdDrained) osSemaphoreDelete(lcdDrained);
    lcdDrained = nullptr;
    stream_free_memory();
}
}

int encodec_stream_start(const HardwareParams& hardware)
{
    finished.store(false, std::memory_order_relaxed);
    schedulerResult = CG_SUCCESS;
    if (stream_init_memory() != 0 || !cg_streamEvent || !cg_streamReplyEvent) {
        stream_free_memory();
        return CG_MEMORY_ALLOCATION_FAILURE;
    }
    eventQueue = new (std::nothrow) ReplayEventQueue;
    lcdDrained = osSemaphoreNew(1, 0, nullptr);
    if (!eventQueue || !eventQueue->ready() || !lcdDrained) {
        delete eventQueue;
        eventQueue = nullptr;
        if (lcdDrained) osSemaphoreDelete(lcdDrained);
        lcdDrained = nullptr;
        stream_free_memory();
        return CG_MEMORY_ALLOCATION_FAILURE;
    }

    app_params_set_hardware(&hardware);
    const int error = init_scheduler_app(eventQueue, &appParams);
    if (error != CG_SUCCESS) {
        freeGraph();
        return error;
    }

    encodec::resetCodec();
    reset_fifos_scheduler_app(1);
    EventQueue::setHandler(nullptr, handleEvent);
    context = {runScheduler, reset_fifos_scheduler_app, nullptr, nullptr, getNodeById,
               eventQueue, STREAM_APP_NB_IDENTIFIED_NODES, STREAM_APP_SCHED_LEN};
    eventQueue->resume();
    if (!stream_start_threads(&context)) {
        freeGraph();
        return CG_OS_ERROR;
    }
    return CG_SUCCESS;
}

bool encodec_stream_finished()
{
    return finished.load(std::memory_order_acquire);
}

int encodec_stream_finish()
{
    stream_stop_threads();
    freeGraph();
    return schedulerResult;
}
