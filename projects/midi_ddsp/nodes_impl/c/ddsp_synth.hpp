// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstdint>
namespace midi_ddsp {
// One control interval; all storage is bounded independently of score duration.
// Frequency/phase use precise accumulation, rather than TF's 1000-sample
// float32 chunks. Explicit white noise permits independent oracle validation.
class Synth {
public:
 void reset();
 void prime(const float *raw,const float *white);
 void interval(const float *raw,const float *next,float hz,float next_hz,
               const float *next_white,float *harmonic,float *noise,float *dry);
 // Player-only hard-muted interval with BOTH frequency endpoints exactly0.
 // Harmonic phases have zero integral; retain exact noise lookahead/history.
 void advance_muted_zero(const float *next,const float *next_white);
private:
 void add_noise(const float *raw,const float *white,unsigned offset);
 double phase_[60]{};
 float noise_[256]{};
};
// 48000 exact signed taps, uniformly partitioned into 750 blocks of64.
// Caller supplies two independent 384000-byte banks; neither aliases controls.
class Reverb {
public:
 void init(float *coefficients,float *history,const float *causal_impulse,bool learned=false);
 void process(const float *dry,float *wet);
private:
 float *coefficients_=nullptr,*history_=nullptr;
 float overlap_[64]{};
 unsigned position_=0;
};
// Packed real FFT convention: [DC,Nyquist,Re1,Im1,...,Re63,Im63].
// Firmware uses CMSIS-DSP; the host fallback has the identical layout/scaling.
void real_fft128(float *input,float *output,bool inverse);
}
