// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstddef>
#include <cstdint>
namespace arm::embedded { class EmbeddedModule; }
namespace handwriting {
constexpr unsigned Hidden=400, AlphabetSize=73, MaxCharacters=75, MethodCount=5;
extern const char alphabet[];
extern const char *const method_names[MethodCount];
extern const unsigned input_sizes[MethodCount], output_sizes[MethodCount];
struct Stroke { float dx, dy, pen_up; };
struct State {
    float h[3][Hidden], c[3][Hidden], window[AlphabetSize], kappa[10];
    float phi[MaxCharacters+1];
    uint8_t characters[MaxCharacters+1];
    unsigned length, steps, limit;
    uint32_t rng;
    Stroke previous;
};
// One event thread owns this state and serializes the five model nodes.
class Engine {
public:
    explicit Engine(arm::embedded::EmbeddedModule *module): module_(module) {}
    int dense(unsigned method, const float *input, float *output, uint32_t *ticks=nullptr);
    int begin(const char *sentence, uint32_t seed=42);
    // 0 emits a point, 1 completes normally, 2 reaches the source step bound.
    // Negative values indicate runtime, input or non-finite-state errors.
    int step(Stroke &point);
    // Execute exactly one model and its CPU recurrence/sampling. Calls must
    // follow method order 0..4; the point/status is final after method 4.
    int stage(unsigned method, Stroke &point);
    bool finite() const;
    const State &state() const { return state_; }
    static void cell(const float *gates, float *h, float *c);
    static void window(const float *attention, State &state);
private:
    float uniform();
    arm::embedded::EmbeddedModule *module_;
    State state_{};
    unsigned next_method_=0;
    alignas(32) float input_[876], output_[1600];
};
}
