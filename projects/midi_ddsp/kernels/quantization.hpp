// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstdint>
#include <executorch/runtime/kernel/operator_registry.h>
struct MidiQuantizationStats {
    // Index0 quantize, index1 dequantize; ticks use the400MHz system timer.
    uint64_t ticks[2]{};
    uint32_t calls[2]{},elements[2]{},dtype_mask[2]{},nonzero_points[2]{};
    uint32_t fast[2]{},fallback[2]{};
};
extern MidiQuantizationStats midi_quantization_stats[9];
// The caller sets0..7 before method execution;8collects unlabelled calls.
extern uint32_t midi_quantization_current_method;
void midi_ddsp_quantize_kernel(executorch::runtime::KernelRuntimeContext &,
    executorch::runtime::Span<executorch::runtime::EValue *>);
void midi_ddsp_dequantize_kernel(executorch::runtime::KernelRuntimeContext &,
    executorch::runtime::Span<executorch::runtime::EValue *>);
