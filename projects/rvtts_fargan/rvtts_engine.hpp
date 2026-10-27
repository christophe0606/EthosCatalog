// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstddef>
#include <cstdint>
namespace rvtts {
constexpr unsigned MaxFrames=224;
enum Method { Pre, Gru, Head, Acoustic, MethodCount };
extern const char *const names[MethodCount];
extern const unsigned input_sizes[MethodCount],output_sizes[MethodCount];
struct Timing { uint32_t ticks; uint64_t cycles; uint32_t invocations,overflow; };
int initialize();
int execute(unsigned method,const float *input,float *output,Timing *timing=nullptr);
int feature_stage(unsigned stage,const uint8_t* phones,const uint8_t* stress,const uint16_t* words,unsigned frames,float* output,Timing* timing=nullptr);
int features(const uint8_t *phones,const uint8_t *stress,const uint16_t *words,
             unsigned frames,float *output,Timing *timing=nullptr);
}
