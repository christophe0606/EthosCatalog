// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "player/midi_file.hpp"
#include "player/instruments.hpp"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>
using namespace midi_ddsp;
using Bytes = std::vector<uint8_t>;
static void big(Bytes& out, uint32_t value, unsigned count) {
    while (count--) out.push_back(static_cast<uint8_t>(value >> (count * 8)));
}
static Bytes file(std::initializer_list<Bytes> tracks, uint16_t ppq = 480) {
    Bytes out{'M','T','h','d'};
    big(out, 6, 4); big(out, tracks.size() > 1 ? 1 : 0, 2);
    big(out, static_cast<uint32_t>(tracks.size()), 2); big(out, ppq, 2);
    for (const auto& track : tracks) {
        out.insert(out.end(), {'M','T','r','k'});
        big(out, static_cast<uint32_t>(track.size()), 4);
        out.insert(out.end(), track.begin(), track.end());
    }
    return out;
}
int main(int argc, char** argv) {
    static_assert(instrument_count == 13, "All thirteen trained instruments must remain selectable");
    for (size_t i = 0; i < instrument_count; ++i)
        assert(instrument_id(instruments[i].gm_program) == static_cast<int>(i));
    assert(instrument_id(0) == -1 && instrument_id(127) == -1);
    MidiEvent events[32];
    // Program, running-status note-on/velocity-zero release, explicit note-off.
    auto basic = file({{0,0xc0,40, 0,0x90,60,100, 0x83,0x60,60,0,
                       0,0x80,64,37, 0,0xff,0x2f,0}});
    auto score = parse_midi_file(basic.data(), basic.size(), events, 32);
    assert(score.error == MidiError::none && score.event_count == 4);
    assert(score.duration_us == 500000 && score.end_tick == 480);
    assert(events[0].type == MidiEventType::program && events[0].value == 40);
    assert(events[1].type == MidiEventType::note_on && events[1].key == 60 && events[1].value == 100);
    assert(events[2].type == MidiEventType::note_off && events[2].time_us == 500000);
    assert(events[3].value == 37);

    // Tempo conductor track changes every track's time, including silent tail.
    auto merged = file({{0,0xff,0x51,3,0x07,0xa1,0x20,
                         0x83,0x60,0xff,0x51,3,0x0f,0x42,0x40,
                         0x87,0x40,0xff,0x2f,0},
                        {0,0x91,65,80, 0x87,0x40,0x81,65,0, 0,0xff,0x2f,0}});
    score = parse_midi_file(merged.data(), merged.size(), events, 32);
    assert(score.error == MidiError::none && score.tracks == 2);
    assert(score.duration_us == 2500000 && score.end_tick == 1440);
    assert(events[3].tick == 960 && events[3].time_us == 1500000);
    assert(events[3].channel == 1 && events[3].type == MidiEventType::note_off);

    // Preserve sub-microsecond fractions across event and tempo boundaries.
    auto fraction = file({{0,0x90,60,1, 1,61,1, 1,62,1, 1,63,1,
                           1,0xff,0x2f,0}}, 3);
    score = parse_midi_file(fraction.data(), fraction.size(), events, 32);
    assert(score.error == MidiError::none && events[3].time_us == 500000);
    assert(score.duration_us == 666666);

    // Controllers/bend retained; channel and key pressure correctly consumed.
    auto controls = file({{0,0xb2,64,127, 0,0xe2,0,64,
                          0,0xd2,100, 0,0xa2,60,30,
                          0,0xf0,2,1,0xf7, 0,0xff,1,3,'a','b','c',
                          0,0xff,0x2f,0}});
    score = parse_midi_file(controls.data(), controls.size(), events, 32);
    assert(score.error == MidiError::none && score.event_count == 2);
    assert(events[0].type == MidiEventType::control && events[0].key == 64);
    assert(events[1].type == MidiEventType::pitch_bend && events[1].value == 8192);

    // Every truncated prefix is rejected, without a read outside that prefix.
    for (size_t i = 0; i < basic.size(); ++i)
        assert(parse_midi_file(basic.data(), i, events, 32).error != MidiError::none);
    assert(parse_midi_file(basic.data(), basic.size(), events, 1).error == MidiError::capacity);
    assert(parse_midi_file(nullptr, 1, events, 32).error == MidiError::invalid_data);
    assert(parse_midi_file(basic.data(), basic.size(), nullptr, 32).error == MidiError::invalid_data);
    auto smpte = basic; smpte[12] = 0xe8;
    assert(parse_midi_file(smpte.data(), smpte.size(), events, 32).error == MidiError::unsupported_timebase);
    auto async = basic; async[9] = 2;
    assert(parse_midi_file(async.data(), async.size(), events, 32).error == MidiError::unsupported_format);
    auto bad_vlq = file({{0x80,0x80,0x80,0x80,0,0xff,0x2f,0}});
    assert(parse_midi_file(bad_vlq.data(), bad_vlq.size(), events, 32).error == MidiError::invalid_vlq);
    auto status = file({{0,60,100,0,0xff,0x2f,0}});
    assert(parse_midi_file(status.data(), status.size(), events, 32).error == MidiError::invalid_status);
    auto reset = file({{0,0x90,60,100,0,0xff,1,0,0,60,0,0,0xff,0x2f,0}});
    assert(parse_midi_file(reset.data(), reset.size(), events, 32).error == MidiError::invalid_status);
    auto data = file({{0,0x90,60,0xff,0,0xff,0x2f,0}});
    assert(parse_midi_file(data.data(), data.size(), events, 32).error == MidiError::invalid_data);
    auto zero_tempo = file({{0,0xff,0x51,3,0,0,0,0,0xff,0x2f,0}});
    assert(parse_midi_file(zero_tempo.data(), zero_tempo.size(), events, 32).error == MidiError::invalid_data);
    auto no_end = file({{0,0x90,60,100}});
    assert(parse_midi_file(no_end.data(), no_end.size(), events, 32).error == MidiError::missing_end);
    auto empty = file({{0,0xff,0x2f,0}});
    score = parse_midi_file(empty.data(), empty.size(), nullptr, 0);
    assert(score.error == MidiError::none && score.event_count == 0 && score.duration_us == 0);
    for (int fixture = 1; fixture < argc; ++fixture) {
        std::ifstream input(argv[fixture], std::ios::binary);
        assert(input.good());
        Bytes bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        MidiEvent fixture_events[512];
        auto parsed = parse_midi_file(bytes.data(), bytes.size(), fixture_events, 512);
        assert(parsed.error == MidiError::none);
        unsigned programs = 0, active = 0, note_count = 0;
        bool seen[instrument_count]{};
        for (size_t i = 0; i < parsed.event_count; ++i) {
            const auto& item = fixture_events[i];
            if (item.type == MidiEventType::program) {
                const int id = instrument_id(static_cast<uint8_t>(item.value));
                assert(id >= 0);
                seen[id] = true;
                ++programs;
            } else if (item.type == MidiEventType::note_on) {
                assert(++active == 1); // Baseline fixtures are strictly monophonic.
                ++note_count;
            } else if (item.type == MidiEventType::note_off) {
                assert(active == 1);
                --active;
            }
        }
        assert(!active && note_count == programs * 8);
        if (fixture == 1) {
            assert(programs == 1 && seen[0] && parsed.duration_us == 4500000);
        } else {
            assert(programs == 13 && parsed.duration_us == 58500000);
            for (bool identity : seen) assert(identity);
        }
    }
    std::puts("MIDI parser: PASS (formats 0/1, tempo merge, running status, controllers, bounds)");
}
