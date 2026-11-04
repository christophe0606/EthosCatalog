// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstdint>
struct RNNoiseState { int8_t vad[24],noise[48],denoise[96]; };
void rnnoise_reset(RNNoiseState &state);
// Output order: denoise96, gains22, noise48, VAD24, probability1.
// Inputs/outputs and state must be distinct; state is updated after copying outputs.
int rnnoise_forward(void *module, int8_t *features, RNNoiseState &state,
                    int8_t *outputs, uint32_t *forward_ticks=nullptr);
