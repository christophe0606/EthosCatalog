// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "midi_file.hpp"
#include <array>

namespace midi_ddsp {
constexpr unsigned score_window_frames = 1000;
constexpr unsigned score_frame_us = 4000;
constexpr unsigned score_region_capacity = 64; // Storage bound, not measured work.

struct ScoreRegion {
    uint32_t note_serial = 0; // Zero denotes a rest; distinct reattacks never merge.
    uint16_t first = 0;
    uint16_t length = 0;
    uint8_t pitch = 0;
    uint8_t velocity = 0;
    uint8_t channel = 0;
    bool carried_in = false;
    bool held_at_end = false; // Before events quantized to the next window.
};
struct ScoreFrameControls {
    uint16_t pitch_bend = 8192;
    uint8_t volume = 100;
    uint8_t expression = 127;
    uint8_t pan = 64;
    uint8_t velocity = 0; // Last admitted velocity also persists into normal rests.
};
struct ScoreWindow {
    uint64_t first_frame = 0;
    uint16_t valid_frames = 0;
    uint16_t region_count = 0;
    uint16_t desired_regions = 0;
    uint16_t capacity_rest_start = score_window_frames;
    int8_t instrument = -1;
    bool instrument_changed = false;
    bool has_notes = false;
    std::array<ScoreRegion, score_region_capacity> regions{};
    std::array<uint16_t, score_window_frames> frame_region{};
    std::array<uint8_t, score_window_frames> onset{};
    std::array<uint8_t, score_window_frames> offset{};
    std::array<ScoreFrameControls, score_window_frames> controls{};
};
struct ScoreWindowCounters {
    uint64_t windows = 0;
    uint64_t note_ons = 0;
    uint64_t unsupported_notes = 0; // Unsupported program/percussion/key0(rest token).
    uint64_t rejected_instrument_notes = 0;
    uint64_t overlap_steals = 0;
    uint64_t unmatched_note_offs = 0;
    uint64_t quantized_away_notes = 0;
    uint64_t program_changes = 0;
    uint64_t instrument_switches = 0;
    uint64_t dense_windows = 0;
    uint64_t dense_regions = 0;
    uint64_t dense_note_regions = 0;
    uint64_t capacity_rest_frames = 0;
};

// Sequential score reader, no heap. Events remain caller-owned and must come
// from a successful parse. Absolute times round to nearest4ms, ties upward;
// quotient/remainder arithmetic avoids overflow. Regions clip at model windows.
// The first actually sounding instrument locks each window; other instruments
// are rejected without stealing it. Same-instrument overlap is last-note wins.
// If region work exceeds the supplied measured limit, retain a bounded prefix
// and replace the suffix with rest, counted explicitly. capacity_rest_start
// requires a hard dry mute in playback; MIDI state still advances in real time.
// This is one instrument stream admission, never a claim of playable capacity.
class ScoreWindows {
public:
    bool configure(const MidiEvent *events, size_t count, uint64_t duration_us,
                   unsigned measured_region_limit);
    bool next(ScoreWindow &window);
    const ScoreWindowCounters &counters() const { return counters_; }
    static uint64_t quantized_frame(uint64_t time_us);
private:
    struct Channel {
        uint16_t pitch_bend = 8192;
        uint8_t program = 0, volume = 100, expression = 127, pan = 64;
        bool sustain = false;
    };
    struct Note {
        uint64_t first_frame = 0;
        uint32_t serial = 0, key_serial = 0;
        uint8_t key = 0, channel = 0, velocity = 0, instrument = 0;
        bool sounding = false, key_down = false, observed = false;
    };
    void dispatch(const MidiEvent &event, int selected_instrument);
    void stop_note();
    const MidiEvent *events_ = nullptr;
    size_t count_ = 0, cursor_ = 0;
    uint64_t next_frame_ = 0, total_frames_ = 0;
    uint32_t serial_ = 0;
    unsigned region_limit_ = 0;
    int last_instrument_ = -1;
    uint8_t last_channel_ = 0, last_velocity_ = 0;
    std::array<Channel, 16> channels_{};
    std::array<std::array<uint32_t, 128>, 16> presses_{};
    std::array<std::array<uint32_t, 128>, 16> releases_{};
    Note active_{};
    ScoreWindowCounters counters_{};
};

// Four source-style synthesis features; expression conditioning is separate.
// Rest regions still have onset/offset markers and zero relative position.
void score_frame_features(const ScoreWindow &window, unsigned frame, float out[4]);
}
