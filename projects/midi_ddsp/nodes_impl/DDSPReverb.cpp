// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#define MIDI_REVERB_BACKEND_ONLY
#include "DDSPReverb.hpp"
#include "c/ddsp_reverb256.hpp"
namespace {
alignas(32) float coefficients[midi_ddsp::Reverb256::SpectrumFloats] __attribute__((section(".bss.midi_sram1")));
alignas(32) float history[midi_ddsp::Reverb256::SpectrumFloats];
}
extern "C" midi_reverb_banks_t midi_reverb_banks(void){return {coefficients,history};}
