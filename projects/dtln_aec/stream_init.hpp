// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "hardware_params.h"
bool dtlnPlaybackStopping();
// The graph keeps the hardware bindings until dtln_stream_shutdown().
int dtln_stream_start(const HardwareParams& hardware);
bool dtln_stream_finished();
// After dtln_stream_finished(), wait for the workers to pause or stop before closing SDS handles.
int dtln_stream_finish();
void dtln_stream_shutdown();
