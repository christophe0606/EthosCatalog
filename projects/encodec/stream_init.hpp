// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "hardware_params.h"

bool encodecPlaybackStopping();
int encodec_stream_start(const HardwareParams& hardware);
bool encodec_stream_finished();

// Call after completion and before closing either SDS stream.
int encodec_stream_finish();
