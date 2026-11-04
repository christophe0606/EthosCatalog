// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstddef>
#include <cstdint>

namespace midi_ddsp {
enum class MidiEventType : uint8_t { note_on, note_off, program, control, pitch_bend, tempo };
struct MidiEvent {
    uint64_t tick;
    uint64_t time_us;
    uint32_t order;
    uint32_t value;
    MidiEventType type;
    uint8_t channel;
    uint8_t key;
};
enum class MidiError : uint8_t {
    none, truncated, invalid_header, unsupported_format, unsupported_timebase,
    invalid_track, invalid_vlq, invalid_status, invalid_data, missing_end,
    capacity, time_overflow
};
struct MidiScore {
    size_t event_count = 0;
    size_t error_offset = 0;
    uint64_t end_tick = 0;
    uint64_t duration_us = 0;
    uint16_t ticks_per_quarter = 0;
    uint16_t tracks = 0;
    MidiError error = MidiError::none;
};

// Parse format 0/1 SMF into caller-owned storage, with no heap allocations.
// Tick timing is merged across tracks and converted using the complete tempo
// map. SMPTE timebases and asynchronous format 2 are explicitly unsupported.
// A failed parse leaves event storage unspecified; callers must check error.
MidiScore parse_midi_file(const uint8_t* data, size_t size,
                          MidiEvent* events, size_t capacity);
}
