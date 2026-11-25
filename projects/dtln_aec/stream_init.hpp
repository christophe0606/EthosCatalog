// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "hardware_params.h"
bool dtlnPlaybackStopping();
int dtln_stream_start(const HardwareParams& hardware);
bool dtln_stream_finished();
int dtln_stream_finish();
