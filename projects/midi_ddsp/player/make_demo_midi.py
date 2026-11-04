#!/usr/bin/env python3
# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Create original test scores without a MIDI/SoundFont package dependency.

The solo score is for the first one-voice/instrument measurement. The instrument
score exercises each checkpoint identity sequentially. A polyphonic stress score
can be generated with --voices after the firmware benchmark sets the voice cap.
These MIDI events contain no prerecorded audio or external musical work.
"""
import argparse
from pathlib import Path
import struct

PROGRAMS = (40, 41, 42, 43, 73, 68, 71, 66, 70, 56, 60, 57, 58)
# Keep each short fixture near its instrument's ordinary register.
BASE_NOTES = (67, 60, 48, 40, 76, 69, 64, 57, 48, 64, 55, 48, 40)
PPQ = 480


def vlq(value):
    if not 0 <= value < 2**28:
        raise ValueError("SMF delta time exceeds four-byte VLQ")
    data = [value & 127]
    while value := value >> 7:
        data.insert(0, 128 | (value & 127))
    return bytes(data)


def smf(events, end_tick):
    """Encode ordered (tick, bytes) events into a single-track 120 BPM SMF."""
    track = bytearray(b"\x00\xff\x51\x03\x07\xa1\x20")
    previous = 0
    # Preserve insertion order at equal ticks (note-off before program/note-on).
    for tick, payload in sorted(events, key=lambda item: item[0]):
        track.extend(vlq(tick - previous))
        track.extend(payload)
        previous = tick
    track.extend(vlq(end_tick - previous))
    track.extend(b"\xff\x2f\x00")
    return (b"MThd" + struct.pack(">IHHH", 6, 0, 1, PPQ)
            + b"MTrk" + struct.pack(">I", len(track)) + track)


def phrase(events, start, identity, channel=0, transpose=0):
    events.append((start, bytes((0xc0 | channel, PROGRAMS[identity]))))
    for index, interval in enumerate((0, 4, 7, 2, 5, 9, 7, 0)):
        tick = start + index * PPQ
        note = BASE_NOTES[identity] + interval + transpose
        events.append((tick, bytes((0x90 | channel, note, 72 + index % 4 * 8))))
        events.append((tick + 420, bytes((0x80 | channel, note, 0))))
    return start + 8 * PPQ


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, default=Path(__file__).resolve().parents[1] / "assets")
    parser.add_argument("--voices", type=int, help="Generate an ensemble stress score with this measured voice limit")
    args = parser.parse_args()
    if args.voices is not None and not 1 <= args.voices <= 15:
        parser.error("--voices must be 1..15 (MIDI percussion channel is excluded)")
    args.output_dir.mkdir(parents=True, exist_ok=True)
    solo = []
    end = phrase(solo, 0, 0)
    (args.output_dir / "single_voice.mid").write_bytes(smf(solo, end + PPQ))
    all_instruments = []
    end = 0
    for identity in range(len(PROGRAMS)):
        end = phrase(all_instruments, end, identity) + PPQ
    (args.output_dir / "all_instruments.mid").write_bytes(smf(all_instruments, end))
    if args.voices is not None:
        ensemble = []
        channels = [i for i in range(16) if i != 9]
        for voice in range(args.voices):
            end = phrase(ensemble, 0, voice % len(PROGRAMS), channels[voice])
        (args.output_dir / f"ensemble_{args.voices}_voices.mid").write_bytes(smf(ensemble, end + PPQ))
    print(f"Wrote original MIDI fixtures to {args.output_dir}")


if __name__ == "__main__":
    main()
