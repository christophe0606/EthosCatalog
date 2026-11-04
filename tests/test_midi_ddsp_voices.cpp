// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "player/voice_allocator.hpp"
#include <cassert>
#include <cstdio>
#include <vector>
using namespace midi_ddsp;
struct Action { VoiceActionType kind; unsigned slot; Voice voice; MidiChannel channel; uint64_t time; };
static void collect(void* context, VoiceActionType kind, unsigned slot,
                    const Voice& voice, const MidiChannel& channel, uint64_t time) {
    static_cast<std::vector<Action>*>(context)->push_back({kind, slot, voice, channel, time});
}
static MidiEvent event(MidiEventType kind, unsigned channel, unsigned key, unsigned value, uint64_t time = 0) {
    return {0, time, 0, value, kind, static_cast<uint8_t>(channel), static_cast<uint8_t>(key)};
}
int main() {
    VoiceAllocator allocator;
    std::vector<Action> actions;
    assert(!allocator.configure(0, collect, &actions));
    assert(!allocator.configure(voice_storage_capacity + 1, collect, &actions));
    assert(!allocator.configure(1, nullptr, nullptr));
    assert(allocator.configure(2, collect, &actions));
    allocator.dispatch(event(MidiEventType::program, 0, 0, 40));
    allocator.dispatch(event(MidiEventType::note_on, 0, 60, 100, 1));
    allocator.dispatch(event(MidiEventType::note_on, 0, 64, 100, 2));
    assert(allocator.counters().maximum_active == 2 && actions.size() == 2);
    allocator.dispatch(event(MidiEventType::note_on, 0, 67, 100, 3));
    assert(allocator.counters().stolen == 1 && allocator.voice(0).key == 67);
    assert(actions[2].kind == VoiceActionType::stop && actions[2].voice.key == 60);
    allocator.dispatch(event(MidiEventType::note_off, 0, 64, 0, 4));
    auto prior = allocator.voice(1).serial;
    assert(allocator.voice(1).releasing);
    allocator.dispatch(event(MidiEventType::note_on, 0, 69, 80, 5));
    assert(allocator.voice(1).key == 69 && allocator.counters().stolen == 2);
    allocator.dispatch(event(MidiEventType::note_off, 0, 69, 0, 6));
    allocator.finished(1, prior); // Delayed completion from stolen generation.
    assert(allocator.voice(1).active);
    allocator.finished(1, allocator.voice(1).serial);
    assert(!allocator.voice(1).active);

    // Sustain holds the key after note-off; pedal-up starts one release only.
    actions.clear();
    assert(allocator.configure(2, collect, &actions));
    allocator.dispatch(event(MidiEventType::program, 0, 0, 40));
    allocator.dispatch(event(MidiEventType::control, 0, 64, 127));
    allocator.dispatch(event(MidiEventType::note_on, 0, 60, 70));
    allocator.dispatch(event(MidiEventType::note_off, 0, 60, 0));
    assert(allocator.voice(0).active && !allocator.voice(0).key_down && !allocator.voice(0).releasing);
    allocator.dispatch(event(MidiEventType::control, 0, 64, 0));
    assert(allocator.voice(0).releasing && allocator.counters().released == 1);
    allocator.dispatch(event(MidiEventType::control, 0, 64, 0));
    assert(allocator.counters().released == 1);

    // Same-key note-offs are FIFO; a stolen key cannot release its reattack.
    assert(allocator.configure(1, collect, &actions));
    allocator.dispatch(event(MidiEventType::program, 0, 0, 40));
    allocator.dispatch(event(MidiEventType::note_on, 0, 60, 70));
    allocator.dispatch(event(MidiEventType::note_on, 0, 60, 90));
    allocator.dispatch(event(MidiEventType::note_off, 0, 60, 0));
    assert(allocator.voice(0).key_down && !allocator.voice(0).releasing);
    allocator.dispatch(event(MidiEventType::note_off, 0, 60, 0));
    assert(allocator.voice(0).releasing);

    // Program change does not retimbre an existing note; unsupported instruments
    // and percussion are counted and never silently mapped onto a trained ID.
    assert(allocator.configure(2, collect, &actions));
    allocator.dispatch(event(MidiEventType::note_on, 0, 60, 70)); // GM0 unsupported.
    allocator.dispatch(event(MidiEventType::program, 0, 0, 40));
    allocator.dispatch(event(MidiEventType::note_on, 0, 60, 70));
    allocator.dispatch(event(MidiEventType::note_off, 0, 60, 0));
    assert(allocator.voice(0).key_down);
    allocator.dispatch(event(MidiEventType::program, 0, 0, 73));
    allocator.dispatch(event(MidiEventType::note_on, 0, 72, 70));
    assert(allocator.voice(0).instrument == 0 && allocator.voice(1).instrument == 4);
    allocator.dispatch(event(MidiEventType::program, 9, 0, 40));
    allocator.dispatch(event(MidiEventType::note_on, 9, 36, 70));
    assert(allocator.counters().unsupported_notes == 2 && allocator.counters().stolen == 0);
    allocator.dispatch(event(MidiEventType::pitch_bend, 0, 0, 10000));
    assert(allocator.channel(0).pitch_bend == 10000);
    assert(actions.back().kind == VoiceActionType::controls && actions.back().channel.pitch_bend == 10000);
    allocator.dispatch(event(MidiEventType::control, 0, 7, 80));
    assert(allocator.channel(0).volume == 80);
    allocator.dispatch(event(MidiEventType::control, 0, 123, 0));
    assert(allocator.voice(0).releasing && allocator.voice(1).releasing);
    allocator.dispatch(event(MidiEventType::control, 0, 120, 0));
    assert(!allocator.voice(0).active && !allocator.voice(1).active);
    // An unsupported newer press must not swallow an older supported release.
    assert(allocator.configure(1, collect, &actions));
    allocator.dispatch(event(MidiEventType::program, 0, 0, 40));
    allocator.dispatch(event(MidiEventType::note_on, 0, 60, 70));
    allocator.dispatch(event(MidiEventType::program, 0, 0, 0));
    allocator.dispatch(event(MidiEventType::note_on, 0, 60, 70));
    allocator.dispatch(event(MidiEventType::note_off, 0, 60, 0));
    assert(allocator.voice(0).releasing);
    allocator.finished(0, allocator.voice(0).serial);
    allocator.dispatch(event(MidiEventType::program, 0, 0, 40));
    allocator.dispatch(event(MidiEventType::note_on, 0, 60, 70));
    allocator.dispatch(event(MidiEventType::note_off, 0, 60, 0));
    assert(allocator.voice(0).key_down); // This off belongs to unsupported press.
    std::puts("Voice allocation: PASS (supplied cap, sustain, stealing, identity, stale generations)");
}
