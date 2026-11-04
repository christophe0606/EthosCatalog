#!/usr/bin/env python3
# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Create the original monophonic piece with thirteen aligned instrument sections."""
import argparse
from pathlib import Path
from make_demo_midi import PPQ, PROGRAMS, phrase, smf

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path,
                        default=Path(__file__).resolve().parents[1]/'assets/all_instruments_aligned.mid')
    args = parser.parse_args()
    events = []
    # A four-second melody and four-second rest per instrument. Eight-second
    # sections align program changes with the model's four-second windows.
    for identity in range(len(PROGRAMS)):
        phrase(events, identity*16*PPQ, identity)
    data = smf(events, len(PROGRAMS)*16*PPQ)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(data)
    print(f'Wrote {len(PROGRAMS)} instrument sections, 104 seconds: {args.output}')

if __name__ == '__main__':
    main()
