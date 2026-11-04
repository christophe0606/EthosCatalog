// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstdint>
namespace arm::embedded { class EmbeddedModule; }
struct MidiKernelStats {
    // mean, sub, square, add, rsqrt, mul; unsupported cases call pack kernels.
    uint32_t fast[6]{};
    uint32_t fallback[6]{};
};
extern MidiKernelStats midi_kernel_stats;
struct MidiNormalizationStats {
    uint64_t ticks[6]{};
    uint32_t calls[6]{};
};
// Per-method counters use the quantization current_method label; index8 is
// unlabelled. Define MIDI_DDSP_NORMALIZATION_TIMING=0 to omit timer reads.
extern MidiNormalizationStats midi_normalization_stats[9];
int midi_ddsp_install_normalization(arm::embedded::EmbeddedModule *module);
