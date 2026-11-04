// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstddef>
#include <cstdint>

namespace midi_ddsp {
struct Instrument {
    const char* name;
    uint8_t gm_program;
};
// Order is the trained checkpoint identity, not General MIDI program order.
// Source: magenta/midi-ddsp midi_ddsp/data_handling/instrument_name_utils.py.
// Programs use the MIDI wire encoding (0..127), not display numbers (1..128).
constexpr Instrument instruments[] = {
    {"Violin", 40}, {"Viola", 41}, {"Cello", 42}, {"Double bass", 43},
    {"Flute", 73}, {"Oboe", 68}, {"Clarinet", 71}, {"Saxophone", 66},
    {"Bassoon", 70}, {"Trumpet", 56}, {"Horn", 60}, {"Trombone", 57}, {"Tuba", 58}
};
constexpr size_t instrument_count = sizeof(instruments) / sizeof(instruments[0]);
constexpr int instrument_id(uint8_t gm_program) {
    for (size_t i = 0; i < instrument_count; ++i)
        if (instruments[i].gm_program == gm_program) return static_cast<int>(i);
    return -1;
}
}
