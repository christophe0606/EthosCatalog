// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "app_config.hpp"
#include "IdentifiedNode.hpp"
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
    stream.records.push_back({timeslot, {bytes, bytes + size}});
    return static_cast<int32_t>(size);
}

SdsSourceParams replayParams{};
SdsSinkParams recordParams{};

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

    AppParams params{};
    assert(init_scheduler_app(nullptr, &params) == CG_SUCCESS);
    int error = CG_SUCCESS;
    assert(scheduler_app(&error) == 2);
    assert(error == CG_STOP_SCHEDULER && timing.timeslot == 123);
    free_scheduler_app();

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
    exerciseSource();
    exerciseSink();
    std::puts("SDS generated scheduler, pacing, transfer deadlines and FIFO handling passed");
}
