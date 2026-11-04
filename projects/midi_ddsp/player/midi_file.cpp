// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "midi_file.hpp"
#include <algorithm>
#include <limits>

namespace midi_ddsp {
namespace {
struct Reader {
    const uint8_t* data;
    size_t pos;
    size_t end;
    bool byte(uint8_t& value) {
        if (pos == end) return false;
        value = data[pos++];
        return true;
    }
    bool skip(size_t count) {
        if (count > end - pos) return false;
        pos += count;
        return true;
    }
    bool big(unsigned count, uint32_t& value) {
        if (count > end - pos) return false;
        value = 0;
        while (count--) value = (value << 8) | data[pos++];
        return true;
    }
    bool vlq(uint32_t& value) {
        value = 0;
        for (unsigned i = 0; i < 4; ++i) {
            uint8_t next;
            if (!byte(next)) return false;
            value = (value << 7) | (next & 127U);
            if (!(next & 128U)) return true;
        }
        return false;
    }
};
bool advance_time(uint64_t ticks, uint32_t tempo, uint16_t ppq,
                  uint64_t& time, uint64_t& fraction) {
    const auto max = std::numeric_limits<uint64_t>::max();
    if (ticks > (max - fraction) / tempo) return false;
    uint64_t scaled = ticks * tempo + fraction;
    if (time > max - scaled / ppq) return false;
    time += scaled / ppq;
    fraction = scaled % ppq;
    return true;
}
}

MidiScore parse_midi_file(const uint8_t* data, size_t size,
                          MidiEvent* events, size_t capacity) {
    MidiScore score;
    Reader file{data, 0, size};
    auto fail = [&](MidiError error, size_t offset) {
        score.error = error;
        score.error_offset = offset;
        return score;
    };
    if ((!data && size) || (!events && capacity))
        return fail(MidiError::invalid_data, 0);
    uint32_t magic, header_size, format, tracks, division;
    if (!file.big(4, magic) || !file.big(4, header_size))
        return fail(MidiError::truncated, file.pos);
    if (magic != 0x4d546864U || header_size < 6)
        return fail(MidiError::invalid_header, 0);
    if (!file.big(2, format) || !file.big(2, tracks) || !file.big(2, division) ||
        !file.skip(header_size - 6)) return fail(MidiError::truncated, file.pos);
    if (format > 1 || !tracks || (format == 0 && tracks != 1))
        return fail(MidiError::unsupported_format, 8);
    if (!division || (division & 0x8000U))
        return fail(MidiError::unsupported_timebase, 12);
    score.tracks = static_cast<uint16_t>(tracks);
    score.ticks_per_quarter = static_cast<uint16_t>(division);
    uint32_t order = 0;
    for (unsigned track_index = 0; track_index < tracks; ++track_index) {
        uint32_t track_size;
        if (!file.big(4, magic) || !file.big(4, track_size))
            return fail(MidiError::truncated, file.pos);
        if (magic != 0x4d54726bU) return fail(MidiError::invalid_track, file.pos - 8);
        if (track_size > file.end - file.pos) return fail(MidiError::truncated, file.pos);
        Reader track{data, file.pos, file.pos + track_size};
        file.pos = track.end;
        uint8_t running = 0;
        uint64_t tick = 0;
        bool ended = false;
        while (track.pos < track.end) {
            uint32_t delta;
            if (!track.vlq(delta)) return fail(MidiError::invalid_vlq, track.pos);
            if (tick > std::numeric_limits<uint64_t>::max() - delta)
                return fail(MidiError::time_overflow, track.pos);
            tick += delta;
            uint8_t status;
            if (!track.byte(status)) return fail(MidiError::truncated, track.pos);
            if (status < 0x80) {
                if (!running) return fail(MidiError::invalid_status, track.pos - 1);
                --track.pos;
                status = running;
            }
            MidiEvent event{tick, 0, order, 0, MidiEventType::control,
                            static_cast<uint8_t>(status & 15U), 0};
            bool retain = false;
            if (status < 0xf0) {
                running = status;
                uint8_t first, second = 0;
                if (!track.byte(first)) return fail(MidiError::truncated, track.pos);
                const uint8_t family = status >> 4;
                if (family != 0xc && family != 0xd && !track.byte(second))
                    return fail(MidiError::truncated, track.pos);
                if (first > 127 || second > 127)
                    return fail(MidiError::invalid_data, track.pos);
                event.key = first;
                event.value = second;
                retain = true;
                switch (family) {
                    case 8: event.type = MidiEventType::note_off; break;
                    case 9: event.type = second ? MidiEventType::note_on : MidiEventType::note_off; break;
                    case 0xb: event.type = MidiEventType::control; break;
                    case 0xc: event.type = MidiEventType::program; event.value = first; break;
                    case 0xe: event.type = MidiEventType::pitch_bend;
                              event.value = first | (static_cast<uint32_t>(second) << 7); break;
                    default: retain = false; break; // Channel pressure/key pressure.
                }
            } else {
                running = 0;
                if (status == 0xff) {
                    uint8_t kind;
                    uint32_t length;
                    if (!track.byte(kind)) return fail(MidiError::truncated, track.pos);
                    if (!track.vlq(length)) return fail(MidiError::invalid_vlq, track.pos);
                    if (length > track.end - track.pos) return fail(MidiError::truncated, track.pos);
                    if (kind == 0x2f) {
                        if (length) return fail(MidiError::invalid_data, track.pos);
                        ended = true;
                        break;
                    }
                    if (kind == 0x51) {
                        if (length != 3 || !track.big(3, event.value) || !event.value)
                            return fail(MidiError::invalid_data, track.pos);
                        event.type = MidiEventType::tempo;
                        event.channel = 0;
                        retain = true;
                    } else track.skip(length);
                } else if (status == 0xf0 || status == 0xf7) {
                    uint32_t length;
                    if (!track.vlq(length)) return fail(MidiError::invalid_vlq, track.pos);
                    if (!track.skip(length)) return fail(MidiError::truncated, track.pos);
                } else return fail(MidiError::invalid_status, track.pos - 1);
            }
            if (retain) {
                if (score.event_count == capacity || order == std::numeric_limits<uint32_t>::max())
                    return fail(MidiError::capacity, track.pos);
                events[score.event_count++] = event;
                ++order;
            }
        }
        if (!ended) return fail(MidiError::missing_end, track.pos);
        score.end_tick = std::max(score.end_tick, tick);
    }
    // std::sort does not allocate; order makes equal-tick ordering deterministic.
    if (score.event_count > 1)
        std::sort(events, events + score.event_count, [](const MidiEvent& a, const MidiEvent& b) {
            return a.tick == b.tick ? a.order < b.order : a.tick < b.tick;
        });
    uint64_t previous = 0, time = 0, fraction = 0;
    uint32_t tempo = 500000; // SMF default: 120 quarter notes per minute.
    for (size_t i = 0; i < score.event_count; ++i) {
        auto& event = events[i];
        if (!advance_time(event.tick - previous, tempo, score.ticks_per_quarter, time, fraction))
            return fail(MidiError::time_overflow, size);
        event.time_us = time;
        previous = event.tick;
        if (event.type == MidiEventType::tempo) tempo = event.value;
    }
    if (!advance_time(score.end_tick - previous, tempo, score.ticks_per_quarter, time, fraction))
        return fail(MidiError::time_overflow, size);
    score.duration_us = time;
    return score;
}
}
