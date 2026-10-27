// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstddef>
#include <cstdint>
namespace rvtts {
constexpr unsigned MaxFrames=224;
enum Method { Pre, Gru, Head, Acoustic, Init, Frame, MethodCount };
extern const char *const names[MethodCount];
extern const unsigned input_sizes[MethodCount],output_sizes[MethodCount];
struct Timing { uint64_t ticks; uint64_t cycles; uint32_t invocations,overflow; };
int initialize();
int execute(unsigned method,const float *input,float *output,Timing *timing=nullptr);
int feature_stage(unsigned stage,const uint8_t* phones,const uint8_t* stress,const uint16_t* words,unsigned frames,float* output,Timing* timing=nullptr);
int features(const uint8_t *phones,const uint8_t *stress,const uint16_t *words,
             unsigned frames,float *output,Timing *timing=nullptr);
int vocoder_method(unsigned method, const float *input, const int64_t *indices,
                   float *output, Timing *timing=nullptr);
int synthesize(float *features, unsigned frames, int16_t *pcm, unsigned capacity,
               unsigned *samples, Timing *timing=nullptr);
extern Timing vocoder_full_frame_timing[MaxFrames+2];
struct Timer {
    uint32_t cycles, ticks;
    Timer();
    uint64_t elapsed() const;
};
}

