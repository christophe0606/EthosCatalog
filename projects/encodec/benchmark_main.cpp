// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "codec_runtime.hpp"

#include "cmsis_os2.h"

#include <algorithm>
#include <cstring>

struct EndpointBenchmark {
    uint32_t blocks;
    uint32_t maxTicks;
    uint32_t deadlineMisses;
};

extern "C" {
volatile uint32_t encodecBenchmarkStage;
volatile uint32_t encodecBenchmarkError;
volatile uint32_t encodecBenchmarkBlocks;
volatile uint32_t encodecBenchmarkTimerHz;
volatile uint32_t encodecBenchmarkMaxTicks;
volatile uint32_t encodecBenchmarkMaxEncoderTicks;
volatile uint32_t encodecBenchmarkMaxDecoderTicks;
volatile uint32_t encodecBenchmarkMissedDeadlines;
volatile EndpointBenchmark encodecStandaloneEncoder;
volatile EndpointBenchmark encodecStandaloneDecoder;
}

namespace {

using executorch::runtime::Error;
constexpr unsigned endpointBlocks = 100;
alignas(32) float input[encodec::blockSamples] __attribute__((section(".bss.ai_pool")));
alignas(32) float output[encodec::blockSamples] __attribute__((section(".bss.ai_pool")));
alignas(32) int32_t capturedCodes[endpointBlocks][encodec::blockCodes];

void prepareBlock(unsigned block)
{
    for (unsigned i = 0; i < encodec::blockSamples; ++i) {
        const int phase = static_cast<int>((block * encodec::blockSamples + i) % 149);
        input[i] = (phase - 74) / 296.0f;
    }
}

bool validOutput()
{
    for (float sample : output) {
        uint32_t bits;
        std::memcpy(&bits, &sample, sizeof(bits));
        if ((bits & 0x7f800000U) == 0x7f800000U) return false;
    }
    return true;
}

void recordEndpoint(volatile EndpointBenchmark& stats, uint32_t ticks, uint32_t budget)
{
    stats.maxTicks = std::max(static_cast<uint32_t>(stats.maxTicks), ticks);
    if (ticks > budget) ++stats.deadlineMisses;
    ++stats.blocks;
}

Error benchmarkEndpoints(uint32_t budget)
{
    // Capture the producer's discrete codes before starting the consumer.
    encodec::resetEncoder();
    for (unsigned block = 0; block < endpointBlocks; ++block) {
        prepareBlock(block);
        const auto error = encodec::encodeBlock(input, capturedCodes[block]);
        if (error != Error::Ok) return error;
        recordEndpoint(encodecStandaloneEncoder, encodec::encodecLastEncoderTicks, budget);
    }

    encodec::resetDecoder();
    for (auto& codes : capturedCodes) {
        const auto error = encodec::decodeBlock(codes, output);
        if (error != Error::Ok) return error;
        if (!validOutput()) return Error::InvalidProgram;
        recordEndpoint(encodecStandaloneDecoder, encodec::encodecLastDecoderTicks, budget);
    }
    return Error::Ok;
}

}

extern "C" int app_main()
{
    encodecBenchmarkStage = 1;
    encodecBenchmarkTimerHz = osKernelGetSysTimerFreq();
    auto error = encodec::initializeCodec();
#ifdef ENCODEC_CPU_PROFILING
    if (error == Error::Ok) error = encodec::profileModelCopies();
#endif
    if (error != Error::Ok) {
        encodecBenchmarkError = static_cast<uint32_t>(error);
        return static_cast<int>(error);
    }
    encodecBenchmarkStage = 2;
    const uint32_t budget = uint64_t{encodecBenchmarkTimerHz} * encodec::blockSamples / encodec::sampleRate;
    uint32_t maximum = 0;
    for (unsigned block = 0; block < 12; ++block) {
        prepareBlock(block);
        error = encodec::processBlock(input, output);
        if (error != Error::Ok) break;
        if (!validOutput()) {
            error = Error::InvalidProgram;
            break;
        }
        maximum = std::max(maximum, encodec::encodecLastBlockTicks);
        encodecBenchmarkMaxTicks = maximum;
        encodecBenchmarkMaxEncoderTicks = std::max(static_cast<uint32_t>(encodecBenchmarkMaxEncoderTicks),
                                                   encodec::encodecLastEncoderTicks);
        encodecBenchmarkMaxDecoderTicks = std::max(static_cast<uint32_t>(encodecBenchmarkMaxDecoderTicks),
                                                   encodec::encodecLastDecoderTicks);
        if (encodec::encodecLastBlockTicks > budget) ++encodecBenchmarkMissedDeadlines;
        ++encodecBenchmarkBlocks;
    }
    if (error == Error::Ok) error = benchmarkEndpoints(budget);
    encodecBenchmarkError = static_cast<uint32_t>(error);
    encodecBenchmarkStage = 3;
    return static_cast<int>(error);
}
