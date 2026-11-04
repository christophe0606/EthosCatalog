#!/usr/bin/env python3
# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Original finite all-identity score with low notes, held seams and admission pressure."""
from pathlib import Path
from make_demo_midi import PPQ,PROGRAMS,BASE_NOTES,phrase,smf
import argparse

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=Path(__file__).resolve().parents[1]/'assets/validation.mid')
    args=parser.parse_args()
    events=[]
    low=(55,48,36,28,60,57,52,45,36,52,43,36,28)
    for identity in range(13):
        start=identity*32*PPQ # 16 seconds at120BPM; model boundaries at4seconds.
        phrase(events,start,identity)
        events.extend(((start+8*PPQ,bytes((0x90,low[identity],80))),
                       (start+10*PPQ,bytes((0x80,low[identity],0))),
                       (start+11*PPQ,bytes((0x90,BASE_NOTES[identity],88))),
                       (start+17*PPQ,bytes((0x80,BASE_NOTES[identity],0)))))
    start=13*32*PPQ
    events.extend(((start,bytes((0xc0,PROGRAMS[0]))),(start,bytes((0xc1,PROGRAMS[1])))))
    #32 reattacks/rests produce64 regions: cap16 must explicitly mute suffix.
    for n in range(32):
        tick=start+n*120
        events.extend(((tick,bytes((0x90,67+n%5,80))),(tick+60,bytes((0x80,67+n%5,0)))))
        if n==0: # Different instrument while the admitted note is sounding.
            events.extend(((tick+10,bytes((0x91,60,80))),(tick+50,bytes((0x81,60,0)))))
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_bytes(smf(events,start+8*PPQ))
    print(f'Wrote {args.output}; 212 seconds plus synthesized reverb tail')

if __name__=='__main__':main()
