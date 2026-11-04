// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "voice_allocator.hpp"
#include "instruments.hpp"
#include <algorithm>

namespace midi_ddsp {
bool VoiceAllocator::configure(unsigned measured_limit, VoiceCallback callback, void* context) {
    if (!measured_limit || measured_limit > voices_.size() || !callback) return false;
    voices_ = {};
    channels_ = {};
    key_presses_ = {};
    key_releases_ = {};
    counters_ = {};
    serial_ = 0;
    limit_ = measured_limit;
    callback_ = callback;
    context_ = context;
    return true;
}
void VoiceAllocator::emit(VoiceActionType action, unsigned slot, uint64_t time_us) {
    callback_(context_, action, slot, voices_[slot], channels_[voices_[slot].channel], time_us);
}
void VoiceAllocator::release(unsigned slot, uint64_t time_us) {
    auto& voice = voices_[slot];
    if (!voice.active || voice.releasing) return;
    voice.key_down = false;
    voice.releasing = true;
    voice.released_us = time_us;
    ++counters_.released;
    emit(VoiceActionType::release, slot, time_us);
}
void VoiceAllocator::update_controls(unsigned channel_index, uint64_t time_us) {
    for (unsigned i = 0; i < limit_; ++i)
        if (voices_[i].active && voices_[i].channel == channel_index)
            emit(VoiceActionType::controls, i, time_us);
}
void VoiceAllocator::finished(unsigned slot, uint64_t serial) {
    if (slot < limit_ && voices_[slot].releasing && voices_[slot].serial == serial)
        voices_[slot].active = false;
}
void VoiceAllocator::dispatch(const MidiEvent& event) {
    if (!callback_ || event.channel >= channels_.size() || event.key > 127) return;
    auto& channel = channels_[event.channel];
    if (event.type == MidiEventType::program) {
        channel.program = static_cast<uint8_t>(event.value);
    } else if (event.type == MidiEventType::note_on && event.value) {
        const uint64_t key_serial = ++key_presses_[event.channel][event.key];
        int identity = instrument_id(channel.program);
        if (event.channel == 9 || identity < 0) {
            ++counters_.unsupported_notes;
            return;
        }
        unsigned slot = limit_;
        for (unsigned i = 0; i < limit_; ++i)
            if (!voices_[i].active) { slot = i; break; }
        if (slot == limit_) {
            // Prefer the oldest release tail; otherwise steal oldest sounding
            // note. A stop callback lets DSP fade a stolen voice before reuse.
            slot = 0;
            for (unsigned i = 1; i < limit_; ++i) {
                const auto& a = voices_[i];
                const auto& b = voices_[slot];
                if ((a.releasing && !b.releasing) ||
                    (a.releasing == b.releasing && a.serial < b.serial)) slot = i;
            }
            emit(VoiceActionType::stop, slot, event.time_us);
            ++counters_.stolen;
        }
        voices_[slot] = Voice{event.time_us, 0, ++serial_, key_serial, event.channel,
                              event.key, static_cast<uint8_t>(event.value),
                              static_cast<uint8_t>(identity), true, true, false};
        ++counters_.started;
        emit(VoiceActionType::start, slot, event.time_us);
        unsigned active = 0;
        for (unsigned i = 0; i < limit_; ++i) active += voices_[i].active;
        counters_.maximum_active = std::max(counters_.maximum_active, active);
    } else if (event.type == MidiEventType::note_off ||
               (event.type == MidiEventType::note_on && !event.value)) {
        // FIFO pairing handles repeated presses of the same key/channel.
        auto& released = key_releases_[event.channel][event.key];
        if (released == key_presses_[event.channel][event.key]) {
            ++counters_.unmatched_note_offs;
            return;
        }
        const uint64_t key_serial = ++released;
        unsigned slot = limit_;
        for (unsigned i = 0; i < limit_; ++i) {
            const auto& voice = voices_[i];
            if (voice.active && voice.key_down && voice.channel == event.channel &&
                voice.key == event.key && voice.key_serial == key_serial) slot = i;
        }
        if (slot == limit_) return; // Its matching press was unsupported/stolen.
        voices_[slot].key_down = false;
        if (!channel.sustain) release(slot, event.time_us);
    } else if (event.type == MidiEventType::pitch_bend) {
        channel.pitch_bend = static_cast<uint16_t>(event.value);
        update_controls(event.channel, event.time_us);
    } else if (event.type == MidiEventType::control) {
        switch (event.key) {
            case 7: channel.volume = static_cast<uint8_t>(event.value); break;
            case 10: channel.pan = static_cast<uint8_t>(event.value); break;
            case 11: channel.expression = static_cast<uint8_t>(event.value); break;
            case 64:
                channel.sustain = event.value >= 64;
                if (!channel.sustain)
                    for (unsigned i = 0; i < limit_; ++i)
                        if (voices_[i].active && voices_[i].channel == event.channel && !voices_[i].key_down)
                            release(i, event.time_us);
                break;
            case 120: // All Sound Off: stop immediately, including release tails.
                for (unsigned i = 0; i < limit_; ++i)
                    if (voices_[i].active && voices_[i].channel == event.channel) {
                        emit(VoiceActionType::stop, i, event.time_us);
                        voices_[i].active = false;
                    }
                return;
            case 121: // Reset controllers; program and sounding keys persist.
                channel.pitch_bend = 8192;
                channel.expression = 127;
                channel.sustain = false;
                for (unsigned i = 0; i < limit_; ++i)
                    if (voices_[i].active && voices_[i].channel == event.channel && !voices_[i].key_down)
                        release(i, event.time_us);
                break;
            case 123: // All Notes Off obeys sustain, as ordinary note-offs do.
                for (unsigned i = 0; i < limit_; ++i)
                    if (voices_[i].active && voices_[i].channel == event.channel) {
                        voices_[i].key_down = false;
                        if (!channel.sustain) release(i, event.time_us);
                    }
                break;
            default: return;
        }
        update_controls(event.channel, event.time_us);
    }
}
}
