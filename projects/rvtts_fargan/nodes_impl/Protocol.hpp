// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "../../../common/nodes_impl/c/NodeSupport.hpp"
#include "EventQueue.hpp"
#include "rvtts_engine.hpp"
#include <atomic>
#include <cstdio>
#include <cmath>
extern "C" {
#include "../../rvtts_lpc/upstream/tts_frontend.h"
#include "../../rvtts_lpc/upstream/word_vocab_packed.h"
}
namespace rvtts {
struct Work {
    TtsFrontendResult frontend;
    uint16_t words[MaxFrames];
    float features[MaxFrames*20];
    int16_t pcm[(MaxFrames+1)*160];
    uint32_t generation,started,started_tick,frames,audio_frames;
};
struct Waveform { int16_t samples[160]; uint32_t position,total; std::atomic<bool> pending{false}; };
inline uint64_t address(void* p) { return uint64_t(reinterpret_cast<uintptr_t>(p)); }
inline Work* work(Event& e) {return reinterpret_cast<Work*>(uintptr_t(e.get<uint64_t>()));}
}
