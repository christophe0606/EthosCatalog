// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

namespace dtln::waveform {
void start();
void reset();
void finish();
void append(const float* microphone, const float* output);
uint32_t framesPresented();
int32_t displayError();
}
