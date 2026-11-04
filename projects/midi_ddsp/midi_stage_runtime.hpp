// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "arm_embedded_module.hpp"
#include <cstddef>
#include <cstdint>
extern "C" {
struct MidiStageStats {uint64_t prepare_ticks,copy_ticks,memcpy_ticks,cache_clean_ticks,crc_ticks;uint32_t backend_bytes,constant_bytes,constant_bypasses,loads,method_peak,temp_peak,crc_failures;};
extern MidiStageStats midi_stage_stats[8];
extern uint32_t midi_stage_routes[4];
extern int32_t midi_stage_error;
extern uint32_t midi_stage_last_segment[4];
}
void midi_stage_configure(const unsigned char *pte,size_t bytes,uint8_t *method,size_t method_bytes,uint8_t *temp,size_t temp_bytes,uint64_t (*clock)());
arm::embedded::EmbeddedModule *midi_stage_prepare(unsigned index,uint8_t *staging,size_t staging_bytes,executorch::runtime::HierarchicalAllocator *planned=nullptr);
void midi_stage_release();
