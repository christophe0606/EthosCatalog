// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "app_config.hpp"
#include "IdentifiedNode.hpp"
#include "SdsPictureSink.hpp"
#include "SdsPictureSource.hpp"
#include "SdsSink.hpp"
#include "SdsSource.hpp"
#include "scheduler_app.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <deque>
#include <vector>

struct Record {
    uint32_t timeslot;
    std::vector<uint8_t> payload;
};

struct TestStream {
    std::deque<Record> records;
    std::deque<int32_t> errors;
    std::vector<uint32_t> attemptedTimeslots;
    std::vector<uint32_t> writeTicks;
    unsigned readCalls = 0;
    void* readBuffer = nullptr;
    const void* writeBuffer = nullptr;

    template<typename Sample, size_t samples>
    void add(uint32_t timeslot, const std::array<Sample, samples>& values) {
        auto bytes = reinterpret_cast<const uint8_t*>(values.data());
        records.push_back({timeslot, {bytes, bytes + sizeof(Sample) * samples}});
    }
};

extern "C" int32_t sdsRead(sdsId_t id, uint32_t* timeslot, void* buffer, uint32_t size) {
    auto& stream = *static_cast<TestStream*>(id);
    ++stream.readCalls;
    if (!stream.errors.empty()) {
        auto result = stream.errors.front();
        stream.errors.pop_front();
        return result;
    }
    if (stream.records.empty()) return SDS_EOS;
    const auto& record = stream.records.front();
    if (record.payload.size() > size) return SDS_ERROR_PARAMETER;
    const auto bytes = static_cast<int32_t>(record.payload.size());
    stream.readBuffer = buffer;
    std::memcpy(buffer, record.payload.data(), bytes);
    *timeslot = record.timeslot;
    stream.records.pop_front();
    return bytes;
}

extern "C" int32_t sdsWrite(sdsId_t id, uint32_t timeslot, const void* buffer, uint32_t size) {
    auto& stream = *static_cast<TestStream*>(id);
    stream.attemptedTimeslots.push_back(timeslot);
    stream.writeTicks.push_back(osKernelGetTickCount());
    if (!stream.errors.empty()) {
        auto result = stream.errors.front();
        stream.errors.pop_front();
        return result;
    }
    auto bytes = static_cast<const uint8_t*>(buffer);
    stream.writeBuffer = buffer;
    stream.records.push_back({timeslot, {bytes, bytes + size}});
    return static_cast<int32_t>(size);
}

SdsSourceParams replayParams{};
SdsSinkParams recordParams{};
SdsPictureSourceParams pictureReplayParams{};
SdsPictureSinkParams pictureRecordParams{};

std::pmr::synchronized_pool_resource pool;
void* EventQueue::handlerData = nullptr;
EventQueue::AppHandler EventQueue::handler = nullptr;
std::atomic<bool> EventQueue::handlerReady_ = false;

constexpr uint32_t pictureBytes = 8;
struct TestPicturePool {
    alignas(void*) std::byte blocks[2][SdsPictureSourceParams::poolBlockBytes(pictureBytes)];
    unsigned capacity = 1;
    unsigned inUse = 0;
    bool allocated[2]{};
};

uint32_t osMemoryPoolGetBlockSize(osMemoryPoolId_t) { return sizeof(TestPicturePool::blocks[0]); }
uint32_t osMemoryPoolGetCapacity(osMemoryPoolId_t pool) {
    return static_cast<TestPicturePool*>(pool)->capacity;
}

void* osMemoryPoolAlloc(osMemoryPoolId_t id, uint32_t timeout) {
    assert(timeout == 0);
    auto& pool = *static_cast<TestPicturePool*>(id);
    for (unsigned slot = 0; slot < pool.capacity; ++slot) {
        if (pool.allocated[slot]) continue;
        pool.allocated[slot] = true;
        ++pool.inUse;
        return pool.blocks[slot];
    }
    return nullptr;
}

osStatus_t osMemoryPoolFree(osMemoryPoolId_t id, void* block) {
    auto& pool = *static_cast<TestPicturePool*>(id);
    for (unsigned slot = 0; slot < pool.capacity; ++slot) {
        if (block != pool.blocks[slot]) continue;
        assert(pool.allocated[slot]);
        pool.allocated[slot] = false;
        --pool.inUse;
        return osOK;
    }
    assert(false);
    return osError;
}

class TestPictureQueue : public EventQueue {
public:
    bool isEmpty() final { return messages.empty(); }
    void clear() final { messages.clear(); }
    void execute() final {
        while (!isEmpty()) {
            auto message = std::move(messages.front());
            messages.pop_front();
            auto dst = std::get<LocalDestination>(message.destination);
            assert(dst.dst->processEvent(dst.dstPort, std::move(message.event)) == CG_SUCCESS);
        }
    }

    bool accepting = true;
    std::deque<Message> messages;

protected:
    bool push(Message&& message) final {
        if (!accepting) return false;
        messages.push_back(std::move(message));
        return true;
    }
};

using PictureNotice = std::pair<uint32_t, int32_t>;
class PictureNotices : public StreamNode {
public:
    cg_status processEvent(int, Event&& event) final {
        assert(event.wellFormed<int32_t>());
        notices.emplace_back(event.event_id, event.get<int32_t>());
        return CG_SUCCESS;
    }
    std::vector<PictureNotice> notices;
};

static BufferPtr retainPicture(Event& event, uint32_t expectedTimeslot) {
    assert((event.wellFormed<uint32_t, BufferPtr>()));
    BufferPtr retained;
    auto copy = event.clone();
    assert((copy.apply<uint32_t, BufferPtr>([&](uint32_t timeslot, BufferPtr pixels) {
        assert(timeslot == expectedTimeslot && pixels);
        retained = std::move(pixels);
    }) == CG_SUCCESS));
    return retained;
}

static void exerciseScheduler() {
    TestStream replay, capture;
    for (int16_t i = 0; i < 4; ++i) {
        replay.add(99 + 8 * i, std::array<int16_t, 2>{{static_cast<int16_t>(2 * i),
                                                      static_cast<int16_t>(2 * i + 1)}});
    }
    replay.errors = {SDS_NO_DATA, SDS_ERROR};
    capture.errors = {SDS_NO_SPACE};
    test_rtos::tick = 0;
    SdsPlaybackTiming timing;
    replayParams = {&replay, &timing, 250, 2};
    recordParams = {&capture, &timing};

    TestStream pictureReplay, pictureCapture;
    const std::array<uint16_t, 4> pixels{{1, 257, 42, 255}};
    pictureReplay.add(UINT32_MAX, pixels);
    pictureReplay.add(4, pixels);
    TestPicturePool picturePool;
    TestPictureQueue queue;
    pictureReplayParams = {&pictureReplay, &picturePool, pictureBytes, 2};
    pictureRecordParams = {&pictureCapture, pictureBytes, 2};

    AppParams params{};
    assert(init_scheduler_app(&queue, &params) == CG_SUCCESS);
    int error = CG_SUCCESS;
    assert(scheduler_app(&error) == 2);
    assert(error == CG_STOP_SCHEDULER && timing.timeslot == 123);

    auto& source = *static_cast<SdsPictureSource*>(get_scheduler_app_node(STREAM_APP_PICTURE_REPLAY_ID)->obj);
    auto& sink = *static_cast<SdsPictureSink*>(get_scheduler_app_node(STREAM_APP_PICTURE_RECORD_ID)->obj);
    assert(SdsPictureSource::selectors[SdsPictureSource::picture] ==
           SdsPictureSink::selectors[SdsPictureSink::picture]);
    const auto request = [&] { return source.processEvent(0, Event(kDo, kNormalPriority)); };
    pictureReplay.errors = {SDS_NO_DATA, SDS_ERROR};
    assert(request() == CG_SUCCESS && queue.messages.size() == 1 && picturePool.inUse);
    assert(request() == CG_MEMORY_ALLOCATION_FAILURE && pictureReplay.records.size() == 1);
    auto retained = retainPicture(queue.messages.front().event, UINT32_MAX);
    pictureCapture.errors = {SDS_NO_SPACE};
    queue.execute();
    assert(picturePool.inUse && pictureCapture.records.size() == 1);
    assert(pictureCapture.writeBuffer == pictureReplay.readBuffer);
    assert(pictureCapture.records.front().timeslot == UINT32_MAX);
    assert(std::memcmp(pictureCapture.records.front().payload.data(), pixels.data(), pictureBytes) == 0);
    retained.reset();
    assert(!picturePool.inUse);

    assert(request() == CG_SUCCESS && picturePool.inUse);
    queue.clear();
    assert(!picturePool.inUse);

    PictureNotices notices;
    source.subscribe(1, notices, 0);
    assert(request() == CG_SUCCESS && source.lastSdsStatus() == SDS_EOS && !picturePool.inUse);
    assert(queue.messages.front().event.event_id ==
           SdsPictureSource::selectors[SdsPictureSource::pictureEnd]);
    queue.clear();

    pictureReplay.errors = {SDS_NO_DATA, SDS_NO_DATA, SDS_NO_DATA};
    assert(request() == CG_BUFFER_UNDERFLOW && queue.isEmpty() && !picturePool.inUse);
    pictureReplay.add(5, std::array<uint16_t, 1>{{1}});
    assert(request() == CG_BUFFER_ERROR && queue.isEmpty() && !picturePool.inUse);
    pictureReplay.add(6, pixels);
    queue.accepting = false;
    assert(request() == CG_EVENT_QUEUE_FULL && queue.isEmpty() && !picturePool.inUse);
    queue.accepting = true;

    pictureReplay.add(7, pixels);
    assert(request() == CG_SUCCESS);
    retained = retainPicture(queue.messages.front().event, 7);
    SdsPictureSink wrongSize({&pictureCapture, pictureBytes - 1});
    assert(wrongSize.processEvent(0, queue.messages.front().event.clone()) == CG_BUFFER_ERROR);
    pictureCapture.errors = {SDS_NO_SPACE, SDS_NO_SPACE, SDS_NO_SPACE};
    assert(sink.processEvent(0, queue.messages.front().event.clone()) == CG_BUFFER_OVERFLOW);
    assert(pictureCapture.records.size() == 1 && picturePool.inUse);
    queue.clear();
    free_scheduler_app();
    assert(picturePool.inUse);
    retained.reset();
    assert(!picturePool.inUse);

    assert(replay.readCalls == 7);
    assert((capture.attemptedTimeslots == std::vector<uint32_t>{107, 107, 123}));
    assert((capture.writeTicks == std::vector<uint32_t>{10, 11, 26}));
    assert(capture.records.size() == 2);
    for (size_t i = 0; i < capture.records.size(); ++i) {
        assert(capture.records[i].timeslot == 107 + 16 * i);
        std::array<int16_t, 4> samples;
        assert(capture.records[i].payload.size() == sizeof(samples));
        std::memcpy(samples.data(), capture.records[i].payload.data(), sizeof(samples));
        for (size_t j = 0; j < samples.size(); ++j) {
            assert(samples[j] == static_cast<int16_t>(4 * i + j));
        }
    }
}

static void exercisePictureOverflow() {
    TestStream replay;
    TestPicturePool imagePool;
    imagePool.capacity = 2;
    TestPictureQueue queue;
    PictureNotices graphNotices;
    PictureNotices appNotices;
    EventQueue::setHandler(&appNotices, [](int id, void* data, Event&& event) {
        assert(id == 13);
        return static_cast<PictureNotices*>(data)->processEvent(0, std::move(event)) == CG_SUCCESS;
    });

    const std::array<uint16_t, 4> pixels{{1, 2, 3, 4}};
    for (uint32_t slot : {UINT32_MAX - 15, 17U, 51U, 120U, 153U, 186U, 219U, 218U}) {
        replay.add(slot, pixels);
    }
    {
        SdsPictureSource source(&queue, {&replay, &imagePool, pictureBytes, 0, 30,
                                       SdsPictureOverflowPolicy::Skip});
        source.setID(13);
        source.subscribe(0, graphNotices, 0);
        source.subscribe(1, graphNotices, 0);
        assert(source.init() == CG_SUCCESS && imagePool.inUse == 1);
        const auto request = [&] { return source.processEvent(0, Event(kDo, kNormalPriority)); };

        // Keep the first picture live. The request across timestamp wrap consumes one
        // frame into the reserved block when the output pool slot is already held.
        assert(request() == CG_SUCCESS && imagePool.inUse == 2);
        assert(request() == CG_SUCCESS && queue.messages.size() == 1 && replay.records.size() == 6);
        queue.clear();
        assert(request() == CG_SUCCESS && queue.messages.size() == 1);  // 34 ms
        queue.clear();
        assert(request() == CG_SUCCESS && queue.isEmpty());             // 69 ms gap
        assert(request() == CG_SUCCESS && queue.messages.size() == 1);  // resynchronized
        queue.clear();

        queue.accepting = false;
        assert(request() == CG_SUCCESS && queue.isEmpty() && imagePool.inUse == 1);
        queue.accepting = true;
        assert(request() == CG_SUCCESS && queue.messages.size() == 1);
        queue.clear();
        assert(request() == CG_BUFFER_ERROR && queue.isEmpty());        // backward timestamp
    }
    assert(imagePool.inUse == 0);
    const auto dropped = SdsPictureSource::selectors[SdsPictureSource::pictureDropped];
    const auto error = SdsPictureSource::selectors[SdsPictureSource::pictureError];
    assert((graphNotices.notices == std::vector<PictureNotice>{
        {dropped, CG_MEMORY_ALLOCATION_FAILURE}, {dropped, CG_BUFFER_OVERFLOW},
        {dropped, CG_EVENT_QUEUE_FULL}, {error, CG_BUFFER_ERROR}}));
    assert(appNotices.notices == graphNotices.notices);

    replay.add(0, pixels);
    replay.add(41, pixels);
    SdsPictureSource source(&queue, {&replay, &imagePool, pictureBytes, 0, 25});
    source.setID(13);
    source.subscribe(1, graphNotices, 0);
    assert(source.init() == CG_SUCCESS);
    assert(source.processEvent(0, Event(kDo, kNormalPriority)) == CG_SUCCESS);
    assert(source.processEvent(0, Event(kDo, kNormalPriority)) == CG_BUFFER_OVERFLOW);
    assert(graphNotices.notices.back() == PictureNotice(error, CG_BUFFER_OVERFLOW));
    assert(appNotices.notices == graphNotices.notices);
    EventQueue::setHandler(nullptr, nullptr);
}

static void exerciseSource() {
    TestStream replay;
    float storage[4]{};
    FIFO<float, 4, 0, 1> output(storage);
    SdsPlaybackTiming timing;
    SdsSource<float, 2> source(output, {&replay, &timing, 250, 2});
    SdsSource<float, 2> unopened(output, {});
    SdsSource<float, 2> fractionalPeriod(output, {&replay, &timing, 249, 2});
    assert(unopened.init() == CG_INIT_FAILURE && fractionalPeriod.init() == CG_INIT_FAILURE);
    assert(source.init() == CG_SUCCESS);

    test_rtos::tick = 0;
    replay.errors.assign(12, SDS_NO_DATA);
    assert(source.run() == CG_BUFFER_UNDERFLOW && source.lastSdsStatus() == SDS_NO_DATA);
    assert(test_rtos::tick == 8 && replay.readCalls == 9);
    assert(output.nbSamplesInFIFO() == 0 && !timing.started);
    replay.errors = {SDS_ERROR_IO};
    assert(source.run() == CG_BUFFER_ERROR && source.lastSdsStatus() == SDS_ERROR_IO);
    replay.add(13, std::array<float, 1>{{0.5f}});
    assert(source.run() == CG_BUFFER_ERROR && source.lastSdsStatus() == sizeof(float));
    assert(output.nbSamplesInFIFO() == 0 && !timing.started);

    test_rtos::tick = UINT32_MAX - 3;
    replay.add(14, std::array<float, 2>{{0.25f, -0.5f}});
    assert(source.run() == CG_SUCCESS && timing.timeslot == 14);
    assert(timing.nextReleaseTick == 4 && output.nbSamplesInFIFO() == 2);
    assert(output.getReadBuffer(1)[0] == 0.25f);
    replay.add(22, std::array<float, 2>{{0.75f, -1.0f}});
    assert(source.run() == CG_SUCCESS && timing.timeslot == 22);
    assert(test_rtos::tick == 4 && timing.pacingTimerCounts == 8);
    auto samples = output.getReadBuffer(3);
    assert(samples[0] == -0.5f && samples[1] == 0.75f && samples[2] == -1.0f);
    assert(source.run() == CG_STOP_SCHEDULER && source.lastSdsStatus() == SDS_EOS);

    replay.add(23, std::array<float, 2>{{0.25f, -0.5f}});
    assert(source.run() == CG_BUFFER_ERROR && source.lastSdsStatus() == 2 * sizeof(float));
    replay.add(30, std::array<float, 3>{{0.75f, -1.0f, 2.0f}});
    assert(source.run() == CG_BUFFER_ERROR && source.lastSdsStatus() == SDS_ERROR_PARAMETER);
    replay.records.clear();
    replay.errors.assign(12, SDS_NO_DATA);
    test_rtos::tick = 10; // Two ticks remain after the previous hop used six.
    const auto callsBeforeDeadline = replay.readCalls;
    assert(source.run() == CG_BUFFER_UNDERFLOW && source.lastSdsStatus() == SDS_NO_DATA);
    assert(test_rtos::tick == 12 && replay.readCalls == callsBeforeDeadline + 3);
    assert(output.nbSamplesInFIFO() == 0 && timing.timeslot == 22);

    test_rtos::tick = 11;
    test_rtos::wakeLatency = 2;
    const auto callsBeforeLateWake = replay.readCalls;
    assert(source.run() == CG_BUFFER_UNDERFLOW);
    assert(replay.readCalls == callsBeforeLateWake + 1);
    test_rtos::wakeLatency = 0;
}

static void exerciseSink() {
    TestStream capture;
    uint16_t storage[4]{};
    FIFO<uint16_t, 4> input(storage);
    SdsPlaybackTiming timing{20, 28, 0, true};
    SdsSink<uint16_t, 2> sink(input, {&capture, &timing});
    SdsSink<uint16_t, 2> noClock(input, {&capture});
    assert(noClock.init() == CG_INIT_FAILURE && sink.init() == CG_SUCCESS);
    const std::array<uint16_t, 2> data{{12, 34}};
    std::copy(data.begin(), data.end(), input.getWriteBuffer(2));
    test_rtos::tick = 26;
    capture.errors = {SDS_NO_SPACE, SDS_NO_SPACE, SDS_NO_SPACE, 1, SDS_ERROR_IO};
    assert(sink.run() == CG_BUFFER_OVERFLOW && sink.lastSdsStatus() == SDS_NO_SPACE);
    assert((capture.writeTicks == std::vector<uint32_t>{26, 27, 28}));
    assert((capture.attemptedTimeslots == std::vector<uint32_t>{20, 20, 20}));
    assert(sink.run() == CG_BUFFER_ERROR && sink.lastSdsStatus() == 1);
    assert(sink.run() == CG_BUFFER_ERROR && sink.lastSdsStatus() == SDS_ERROR_IO);
    assert(input.nbSamplesInFIFO() == 2 && capture.records.empty());
    assert(sink.run() == CG_SUCCESS && input.nbSamplesInFIFO() == 0);
    assert(capture.records.size() == 1 && capture.records.front().timeslot == 20);
    assert(std::memcmp(data.data(), capture.records.front().payload.data(), sizeof(data)) == 0);
}

int main() {
    exerciseScheduler();
    exercisePictureOverflow();
    exerciseSource();
    exerciseSink();
    std::puts("SDS audio scheduling and picture event transfers with pool-backed buffers passed");
}
