// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "SdsEndpoint.hpp"

namespace dtln::waveform {
void start();
void reset();
void finish();
SdsRecordObserver microphoneObserver();
SdsRecordObserver outputObserver();
uint32_t framesPresented();
int32_t displayError();
}
