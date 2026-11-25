// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstdint>
namespace arm::embedded { class EmbeddedModule; }
struct DtlnKernelStats {
    // mean, sub, square, add, rsqrt, mul; unsupported cases call pack kernels.
    uint32_t fast[6]{};
    uint32_t fallback[6]{};
};
extern DtlnKernelStats dtln_kernel_stats;
struct DtlnNormalizationStats {
    uint64_t ticks[6]{};
    uint32_t calls[6]{};
};
// Per-method counters use the quantization current_method label; index8 is
// unlabelled. Define DTLN_NORMALIZATION_TIMING=0 to omit timer reads.
extern DtlnNormalizationStats dtln_normalization_stats[9];
int dtln_install_normalization(arm::embedded::EmbeddedModule *module);
