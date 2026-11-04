// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "score_windows.hpp"
#include "instruments.hpp"
#include <algorithm>
#include <cstring>
#include <limits>

namespace midi_ddsp {
uint64_t ScoreWindows::quantized_frame(uint64_t time_us) {
    return time_us/score_frame_us+(time_us%score_frame_us >= score_frame_us/2);
}

bool ScoreWindows::configure(const MidiEvent *events, size_t count,
                            uint64_t duration_us, unsigned measured_region_limit) {
    if ((!events && count) || count > std::numeric_limits<uint32_t>::max() ||
        !measured_region_limit || measured_region_limit > score_region_capacity) return false;
    for (size_t i = 0; i < count; ++i) {
        const auto &event = events[i];
        if (event.time_us > duration_us || (i && event.time_us < events[i-1].time_us) ||
            event.channel >= 16 || event.key > 127) return false;
        if ((event.type == MidiEventType::note_on || event.type == MidiEventType::note_off ||
             event.type == MidiEventType::program || event.type == MidiEventType::control) && event.value > 127) return false;
        if (event.type == MidiEventType::pitch_bend && event.value > 16383) return false;
    }
    events_ = events;
    count_ = count;
    cursor_ = 0;
    next_frame_ = 0;
    total_frames_ = duration_us/score_frame_us+(duration_us%score_frame_us != 0);
    serial_ = 0;
    region_limit_ = measured_region_limit;
    last_instrument_ = -1;
    last_channel_ = last_velocity_ = 0;
    for (auto &channel : channels_) channel = Channel{};
    std::memset(presses_.data(), 0, sizeof(presses_));
    std::memset(releases_.data(), 0, sizeof(releases_));
    active_ = Note{};
    counters_ = ScoreWindowCounters{};
    return true;
}

void ScoreWindows::stop_note() {
    if (active_.sounding && !active_.observed) ++counters_.quantized_away_notes;
    active_.sounding = false;
    active_.key_down = false;
}

void ScoreWindows::dispatch(const MidiEvent &event, int selected) {
    auto &channel = channels_[event.channel];
    if (event.type == MidiEventType::program) {
        if (channel.program != event.value) ++counters_.program_changes;
        channel.program = static_cast<uint8_t>(event.value);
    } else if (event.type == MidiEventType::note_on && event.value) {
        const uint32_t key_serial = ++presses_[event.channel][event.key];
        ++counters_.note_ons;
        ++serial_;
        const int instrument = instrument_id(channel.program);
        if (event.channel == 9 || instrument < 0 || event.key == 0) {
            ++counters_.unsupported_notes;
            return;
        }
        if (selected >= 0 && instrument != selected) {
            ++counters_.rejected_instrument_notes;
            return;
        }
        if (active_.sounding) {
            ++counters_.overlap_steals;
            stop_note();
        }
        active_ = Note{quantized_frame(event.time_us), serial_, key_serial,
                       event.key, event.channel, static_cast<uint8_t>(event.value),
                       static_cast<uint8_t>(instrument), true, true, false};
        last_channel_ = event.channel;
        last_velocity_ = static_cast<uint8_t>(event.value);
    } else if (event.type == MidiEventType::note_off ||
               (event.type == MidiEventType::note_on && !event.value)) {
        auto &released = releases_[event.channel][event.key];
        if (released == presses_[event.channel][event.key]) {
            ++counters_.unmatched_note_offs;
            return;
        }
        const uint32_t identity = ++released;
        if (active_.sounding && active_.key_down && active_.channel == event.channel &&
            active_.key == event.key && active_.key_serial == identity) {
            active_.key_down = false;
            if (!channel.sustain) stop_note();
        }
    } else if (event.type == MidiEventType::pitch_bend) {
        channel.pitch_bend = static_cast<uint16_t>(event.value);
    } else if (event.type == MidiEventType::control) {
        switch (event.key) {
        case 7: channel.volume = static_cast<uint8_t>(event.value); break;
        case 10: channel.pan = static_cast<uint8_t>(event.value); break;
        case 11: channel.expression = static_cast<uint8_t>(event.value); break;
        case 64:
            channel.sustain = event.value >= 64;
            if (!channel.sustain && active_.channel == event.channel && !active_.key_down) stop_note();
            break;
        case 120:
            if (active_.channel == event.channel) stop_note();
            break;
        case 121:
            channel.pitch_bend = 8192;
            channel.expression = 127;
            channel.sustain = false;
            if (active_.channel == event.channel && !active_.key_down) stop_note();
            break;
        case 123:
            if (active_.channel == event.channel) {
                active_.key_down = false;
                if (!channel.sustain) stop_note();
            }
            break;
        default: break;
        }
    }
}

bool ScoreWindows::next(ScoreWindow &out) {
    if (!region_limit_ || next_frame_ >= total_frames_) return false;
    out = ScoreWindow{};
    out.first_frame = next_frame_;
    out.valid_frames = static_cast<uint16_t>(std::min<uint64_t>(score_window_frames, total_frames_-next_frame_));
    int selected = -1;
    uint32_t previous_identity = 0;
    unsigned stored = 0, dense_note_regions = 0;
    bool dense = false;
    for (unsigned f = 0; f < score_window_frames; ++f) {
        const uint64_t frame = next_frame_+f;
        while (cursor_ < count_ && quantized_frame(events_[cursor_].time_us) <= frame) {
            dispatch(events_[cursor_], selected);
            ++cursor_;
        }
        const bool sounding = f < out.valid_frames && active_.sounding;
        if (sounding && selected < 0) selected = active_.instrument;
        const uint32_t identity = sounding ? active_.serial : 0;
        const uint8_t pitch = sounding ? active_.key : 0;
        if (sounding) active_.observed = true;
        const auto &channel = channels_[last_channel_];
        out.controls[f] = ScoreFrameControls{channel.pitch_bend, channel.volume,
                                             channel.expression, channel.pan, last_velocity_};
        if (f == 0 || identity != previous_identity) {
            ++out.desired_regions;
            if (!dense && stored < region_limit_) {
                out.regions[stored++] = ScoreRegion{identity, static_cast<uint16_t>(f), 0,
                    pitch, sounding ? active_.velocity : uint8_t(0),
                    sounding ? active_.channel : uint8_t(0),
                    sounding && active_.first_frame < next_frame_, false};
            } else if (!dense) {
                dense = true;
                auto &last = out.regions[stored-1];
                if (last.pitch) ++dense_note_regions;
                if (pitch) ++dense_note_regions;
                const uint16_t first = last.first, length = last.length;
                last = ScoreRegion{0, first, length, 0, 0, 0, false, false};
                if (stored > 1 && out.regions[stored-2].pitch == 0) {
                    out.regions[stored-2].length += length;
                    --stored;
                }
                out.capacity_rest_start = out.regions[stored-1].first;
                for (unsigned i = out.capacity_rest_start; i < f; ++i) out.frame_region[i] = static_cast<uint16_t>(stored-1);
            } else if (pitch) ++dense_note_regions;
        }
        ++out.regions[stored-1].length;
        out.frame_region[f] = static_cast<uint16_t>(stored-1);
        previous_identity = identity;
    }
    out.region_count = static_cast<uint16_t>(stored);
    for (unsigned r = 0; r < stored; ++r) {
        auto &region = out.regions[r];
        out.onset[region.first] = 1;
        out.offset[region.first+region.length-1] = 1;
        out.has_notes = out.has_notes || region.pitch != 0;
        region.held_at_end = region.pitch != 0 && region.first+region.length == score_window_frames &&
                             active_.sounding && region.note_serial == active_.serial;
    }
    if (dense) {
        ++counters_.dense_windows;
        counters_.dense_regions += out.desired_regions-(stored-1);
        counters_.dense_note_regions += dense_note_regions;
        counters_.capacity_rest_frames += score_window_frames-out.capacity_rest_start;
        for (unsigned f = out.capacity_rest_start; f < score_window_frames; ++f) out.controls[f].velocity = 0;
    }
    out.instrument = static_cast<int8_t>(selected < 0 ? last_instrument_ : selected);
    if (out.instrument >= 0) {
        out.instrument_changed = last_instrument_ >= 0 && out.instrument != last_instrument_;
        if (out.instrument_changed) ++counters_.instrument_switches;
        last_instrument_ = out.instrument;
    }
    next_frame_ += score_window_frames;
    ++counters_.windows;
    return true;
}

void score_frame_features(const ScoreWindow &window, unsigned frame, float out[4]) {
    if (frame >= score_window_frames || !window.region_count) {
        std::fill(out, out+4, 0.f);
        return;
    }
    const unsigned index = window.frame_region[frame];
    const auto &region = window.regions[index];
    out[0] = float(region.pitch)/127.f;
    out[1] = float(window.onset[frame]);
    out[2] = float(window.offset[frame]);
    out[3] = region.pitch && index < 100 ? float(frame-region.first+1)/region.length : 0.f;
}
}
