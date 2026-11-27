// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <executorch/runtime/core/error.h>

namespace encodec {

inline constexpr unsigned sampleRate = 24000;
inline constexpr unsigned framesPerBlock = 9;
inline constexpr unsigned blockSamples = framesPerBlock * 320;
inline constexpr unsigned codebooks = 8;
inline constexpr unsigned blockCodes = codebooks * framesPerBlock;
inline constexpr unsigned methodCount = 10;

struct MethodStats {
    uint32_t calls;
    uint32_t lastTicks;
    uint32_t maxTicks;
    uint32_t npuCalls;
    uint32_t npuOverflow;
    uint32_t weightsAddress;
    uint64_t npuCycles;
};

extern "C" {
extern MethodStats encodecMethodStats[methodCount];
extern uint32_t encodecModelCrc;
extern uint32_t encodecLastBlockTicks;
extern uint32_t encodecLastEncoderTicks;
extern uint32_t encodecLastDecoderTicks;
}

const char* methodName(unsigned index);
executorch::runtime::Error initializeCodec();
void resetEncoder();
void resetDecoder();
void resetCodec();
// Codes are arranged as [codebook][frame]. Calls share one inference arena and
// must run serially when an encoder and decoder reside on the same target.
executorch::runtime::Error encodeBlock(float* input, int32_t* codes);
executorch::runtime::Error decodeBlock(int32_t* codes, float* output);
executorch::runtime::Error processBlock(float* input, float* output);

#ifdef ENCODEC_CPU_PROFILING
unsigned profileMethod();
executorch::runtime::Error profileModelCopies();
#endif

}
