# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Record bounds and CRCs for every serialized external PTE segment."""
import argparse,hashlib,json,zlib
from pathlib import Path
from executorch.exir._serialize._program import _get_extended_header
from executorch.exir._serialize._flatbuffer_program import _flatbuffer_to_program

def main():
    here=Path(__file__).resolve().parent
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pte',type=Path,default=here.parent/'ai_layer/model.pte')
    parser.add_argument('--output-dir',type=Path,default=here)
    args=parser.parse_args()
    data=args.pte.read_bytes();header=_get_extended_header(data)
    if not header or not header.is_valid() or not header.segment_base_offset:raise ValueError('Expected segmented PTE')
    program=_flatbuffer_to_program(data[:header.program_size]);segments=[]
    for index,s in enumerate(program.segments):
        offset=header.segment_base_offset+s.offset
        if offset<header.program_size or offset+s.size>len(data):raise ValueError('Segment outside PTE')
        segments.append(dict(index=index,offset=offset,bytes=s.size,crc=zlib.crc32(data[offset:offset+s.size])))
    args.output_dir.mkdir(parents=True,exist_ok=True)
    lines=['// Copyright 2026 Arm Limited and/or its affiliates.','// SPDX-License-Identifier: Apache-2.0','#pragma once',
           'struct MidiSegmentManifest {unsigned offset,bytes,crc;};','static const MidiSegmentManifest midi_segment_manifest[]={']
    lines += [f'{{{s["offset"]}U,{s["bytes"]}U,0x{s["crc"]:08x}U}},' for s in segments]
    lines += ['};']
    (args.output_dir/'staging_manifest.h').write_text('\n'.join(lines)+'\n',encoding='utf-8')
    (args.output_dir/'staging_manifest.json').write_text(json.dumps(dict(pte_sha256=hashlib.sha256(data).hexdigest(),segments=segments),indent=2)+'\n',encoding='utf-8')
    print('Manifest:',len(segments),'segments; bytes',sum(s['bytes'] for s in segments))
if __name__=='__main__':main()
