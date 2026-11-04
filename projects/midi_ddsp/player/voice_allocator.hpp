// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "midi_file.hpp"
#include <array>

namespace midi_ddsp {
// Storage bound only. The actual playable limit must come from the board's
// complete inference+synthesis benchmark and is supplied to configure().
constexpr unsigned voice_storage_capacity = 16;
struct Voice {
    uint64_t started_us = 0;
    uint64_t released_us = 0;
    uint64_t serial = 0;
    uint64_t key_serial = 0;
    uint8_t channel = 0;
    uint8_t key = 0;
    uint8_t velocity = 0;
    uint8_t instrument = 0;
    bool active = false;
    bool key_down = false;
    bool releasing = false;
};
struct MidiChannel {
    uint16_t pitch_bend = 8192;
    uint8_t program = 0;
    uint8_t volume = 100;
    uint8_t expression = 127;
    uint8_t pan = 64;
    bool sustain = false;
};
enum class VoiceActionType : uint8_t { start, release, stop, controls };
using VoiceCallback = void (*)(void* context, VoiceActionType action,
                              unsigned slot, const Voice& voice,
                              const MidiChannel& channel, uint64_t time_us);
struct VoiceCounters {
    uint32_t started = 0;
    uint32_t released = 0;
    uint32_t stolen = 0;
    uint32_t unsupported_notes = 0;
    uint32_t unmatched_note_offs = 0;
    uint32_t maximum_active = 0;
};

class VoiceAllocator {
public:
    bool configure(unsigned measured_limit, VoiceCallback callback, void* context);
    void dispatch(const MidiEvent& event);
    // Called by the synthesis owner after the release tail has finished.
    void finished(unsigned slot, uint64_t serial);
    const Voice& voice(unsigned slot) const { return voices_[slot]; }
    const MidiChannel& channel(unsigned index) const { return channels_[index]; }
    const VoiceCounters& counters() const { return counters_; }
    unsigned limit() const { return limit_; }
private:
    void emit(VoiceActionType action, unsigned slot, uint64_t time_us);
    void release(unsigned slot, uint64_t time_us);
    void update_controls(unsigned channel, uint64_t time_us);
    std::array<Voice, voice_storage_capacity> voices_{};
    std::array<MidiChannel, 16> channels_{};
    // FIFO press identities also cover stolen/unsupported notes, so their
    // note-offs cannot release a newer note with the same channel and key.
    std::array<std::array<uint64_t, 128>, 16> key_presses_{};
    std::array<std::array<uint64_t, 128>, 16> key_releases_{};
    VoiceCounters counters_{};
    VoiceCallback callback_ = nullptr;
    void* context_ = nullptr;
    uint64_t serial_ = 0;
    unsigned limit_ = 0;
};
}
