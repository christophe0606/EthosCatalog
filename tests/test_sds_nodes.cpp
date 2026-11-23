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
    if (!stream.errors.empty()) {
        auto result = stream.errors.front();
        stream.errors.pop_front();
        return result;
    }
    auto bytes = static_cast<const uint8_t*>(buffer);
    stream.records.push_back({timeslot, {bytes, bytes + size}});
    return static_cast<int32_t>(size);
}

struct WaitLog {
    std::vector<int32_t> statuses;
    bool keepGoing = true;

    static bool wait(void* context, int32_t status) {
        auto& log = *static_cast<WaitLog*>(context);
        log.statuses.push_back(status);
        return log.keepGoing;
    }
};

uint32_t nextTimeslot(void* context) {
    auto& slot = *static_cast<uint32_t*>(context);
    return slot++;
}

struct RecordLog {
    std::vector<std::vector<uint8_t>> records;

    static void observe(void* context, const void* record, uint32_t bytes) {
        auto& log = *static_cast<RecordLog*>(context);
        const auto* first = static_cast<const uint8_t*>(record);
        log.records.emplace_back(first, first + bytes);
    }
};

SdsSourceParams replayParams{};
SdsSinkParams recordParams{};

static void exerciseScheduler() {
    TestStream replay, capture;
    for (int16_t i = 0; i < 4; ++i) {
        replay.add(99 + i, std::array<int16_t, 2>{{static_cast<int16_t>(2 * i),
                                                  static_cast<int16_t>(2 * i + 1)}});
    }
    replay.errors = {SDS_NO_DATA, SDS_ERROR};
    capture.errors = {SDS_NO_SPACE};
    uint32_t playedTimeslot = 0, captureTimeslot = 50;
    WaitLog sourceWait, sinkWait;
    RecordLog sourceRecords, sinkRecords;
    replayParams = {&replay, &playedTimeslot, {WaitLog::wait, &sourceWait, 2}, {RecordLog::observe, &sourceRecords}};
    recordParams = {&capture, nextTimeslot, &captureTimeslot, {WaitLog::wait, &sinkWait, 1}, {RecordLog::observe, &sinkRecords}};

    AppParams params{};
    assert(init_scheduler_app(nullptr, &params) == CG_SUCCESS);
    int error = CG_SUCCESS;
    assert(scheduler_app(&error) == 2);
    assert(error == CG_STOP_SCHEDULER && playedTimeslot == 102);
    free_scheduler_app();

    assert((sourceWait.statuses == std::vector<int32_t>{SDS_NO_DATA, SDS_ERROR}));
    assert((sinkWait.statuses == std::vector<int32_t>{SDS_NO_SPACE}));
    assert((capture.attemptedTimeslots == std::vector<uint32_t>{50, 50, 51}));
    assert(capture.records.size() == 2);
    assert(sourceRecords.records.size() == 4 && sinkRecords.records.size() == 2);
    for (size_t i = 0; i < 2; ++i) {
        assert(capture.records[i].timeslot == 50 + i);
        assert(capture.records[i].payload.size() == 4 * sizeof(int16_t));
        assert(sinkRecords.records[i] == capture.records[i].payload);
        for (size_t input = 0; input < 2; ++input) {
            const auto& source = sourceRecords.records[2 * i + input];
            assert(source.size() == 2 * sizeof(int16_t));
            assert(std::equal(source.begin(), source.end(), capture.records[i].payload.begin() + input * source.size()));
        }
        std::array<int16_t, 4> observed;
        std::memcpy(observed.data(), capture.records[i].payload.data(), sizeof(observed));
        for (size_t j = 0; j < observed.size(); ++j) {
            assert(observed[j] == static_cast<int16_t>(4 * i + j));
        }
    }
}

static void exerciseSource() {
    TestStream replay;
    float storage[4]{};
    FIFO<float, 4, 0, 1> output(storage);
    uint32_t timeslot = 12;
    RecordLog records;
    auto pace = [](uint32_t inputTimeslot) {
        return inputTimeslot == 16 ? CG_STOP_SCHEDULER : CG_SUCCESS;
    };
    SdsSource<float, 2> source(output, {&replay, &timeslot, {}, {RecordLog::observe, &records}, pace});
    SdsSource<float, 2> unopened(output, {});
    SdsSource<float, 2> invalidRetry(output, {&replay, nullptr, {nullptr, nullptr, 1}});
    assert(unopened.init() == CG_INIT_FAILURE && invalidRetry.init() == CG_INIT_FAILURE);
    assert(source.init() == CG_SUCCESS);

    replay.errors = {SDS_NO_DATA, SDS_ERROR_IO};
    assert(source.run() == CG_BUFFER_UNDERFLOW && source.lastSdsStatus() == SDS_NO_DATA);
    assert(source.run() == CG_BUFFER_ERROR && source.lastSdsStatus() == SDS_ERROR_IO);
    replay.add(13, std::array<float, 1>{{0.5f}});
    assert(source.run() == CG_BUFFER_ERROR && source.lastSdsStatus() == sizeof(float));
    assert(output.nbSamplesInFIFO() == 0 && timeslot == 12);
    replay.add(14, std::array<float, 2>{{0.25f, -0.5f}});
    assert(source.run() == CG_SUCCESS && timeslot == 14);
    assert(output.nbSamplesInFIFO() == 2);
    auto first = output.getReadBuffer(1);
    assert(first[0] == 0.25f);
    replay.add(15, std::array<float, 2>{{0.75f, -1.0f}});
    assert(source.run() == CG_SUCCESS && timeslot == 15);
    assert(source.prepareForRunning() == CG_SKIP_EXECUTION);
    const auto calls = replay.readCalls;
    assert(source.run() == CG_BUFFER_OVERFLOW && replay.readCalls == calls);
    auto samples = output.getReadBuffer(3);
    assert(samples[0] == -0.5f && samples[1] == 0.75f && samples[2] == -1.0f);
    assert(source.run() == CG_STOP_SCHEDULER && source.lastSdsStatus() == SDS_EOS);
    replay.add(16, std::array<float, 2>{{0.25f, -0.5f}});
    assert(source.run() == CG_STOP_SCHEDULER && source.lastSdsStatus() == 2 * sizeof(float));
    replay.add(17, std::array<float, 3>{{0.75f, -1.0f, 2.0f}});
    assert(source.run() == CG_BUFFER_ERROR && source.lastSdsStatus() == SDS_ERROR_PARAMETER);
    assert(records.records.size() == 2);
    assert(output.nbSamplesInFIFO() == 0 && timeslot == 15);
}

static void exerciseSink() {
    TestStream capture;
    uint16_t storage[4]{};
    FIFO<uint16_t, 4> input(storage);
    uint32_t timeslot = 20;
    WaitLog log;
    SdsSink<uint16_t, 2> sink(input, {&capture, nextTimeslot, &timeslot,
                                     {WaitLog::wait, &log, 1}});
    SdsSink<uint16_t, 2> noClock(input, {&capture});
    assert(noClock.init() == CG_INIT_FAILURE && sink.init() == CG_SUCCESS);
    assert(sink.prepareForRunning() == CG_SKIP_EXECUTION);
    assert(sink.run() == CG_BUFFER_UNDERFLOW && capture.attemptedTimeslots.empty());

    const std::array<uint16_t, 2> data{{12, 34}};
    std::copy(data.begin(), data.end(), input.getWriteBuffer(2));
    capture.errors = {SDS_NO_SPACE, SDS_NO_SPACE, 1, SDS_ERROR_IO};
    assert(sink.run() == CG_BUFFER_OVERFLOW && sink.lastSdsStatus() == SDS_NO_SPACE);
    assert((capture.attemptedTimeslots == std::vector<uint32_t>{20, 20}));
    assert(sink.run() == CG_BUFFER_ERROR && sink.lastSdsStatus() == 1);
    assert(sink.run() == CG_BUFFER_ERROR && sink.lastSdsStatus() == SDS_ERROR_IO);
    assert(input.nbSamplesInFIFO() == 2 && capture.records.empty());
    capture.errors = {SDS_NO_SPACE};
    log.keepGoing = false;
    assert(sink.run() == CG_STOP_SCHEDULER && sink.lastSdsStatus() == SDS_NO_SPACE);
    assert(input.nbSamplesInFIFO() == 2 && capture.records.empty());
    assert(sink.run() == CG_SUCCESS && input.nbSamplesInFIFO() == 0);
    assert(capture.records.size() == 1 && capture.records.front().timeslot == 24);
    assert(std::memcmp(data.data(), capture.records.front().payload.data(), sizeof(data)) == 0);
}

int main() {
    exerciseScheduler();
    exerciseSource();
    exerciseSink();
    std::puts("SDS generated scheduler, complete record transfer and FIFO error handling passed");
}
