// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once

namespace midi_ddsp {
// Exact 48000-tap FIR with unity dry. Four 64-sample synthesis intervals form
// one 256-sample call: maximum extra accumulation delay is 192 samples (12ms).
// Caller owns disjoint coefficient/history banks and a separate impulse input.
// Each bank needs SpectrumFloats floats (385024 bytes), preferably aligned32.
class Reverb256 {
public:
    static constexpr unsigned Hop = 256;
    static constexpr unsigned Fft = 512;
    static constexpr unsigned Partitions = 188;
    static constexpr unsigned SpectrumFloats = Partitions * Fft;

    // learned=true accepts the original signed taps: suppress tap0 and apply
    // the upstream 16000-sample unity +32000-sample exponential envelope.
    // learned=false accepts already causal/decayed taps. init resets history.
    void init(float *coefficients, float *history, const float *impulse,
              bool learned = false);
    // Exactly256 input/output samples; disjoint buffers or full in-place use.
    // Retain this object/storage across calls, including rests and FIR tails.
    void process256(const float *dry, float *wet);

private:
    float *coefficients_ = nullptr;
    float *history_ = nullptr;
    float overlap_[Hop]{};
    unsigned position_ = 0;
};
}
