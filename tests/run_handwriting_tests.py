# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Compile actual project Stream wrappers/layout using a host compiler."""
import argparse
from pathlib import Path
import subprocess
import struct
import zlib
import numpy as np
ROOT=Path(__file__).resolve().parents[1]
def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--vcvars',type=Path)
    parser.add_argument('--cxx',default='clang++')
    args=parser.parse_args()
    out=ROOT/'out/handwriting-tests';out.mkdir(parents=True,exist_ok=True)
    # Real quantized generated samples concatenated as a long phrase. Retains
    # every real dx/dy/pen triplet; this checks geometry, not text accuracy.
    samples=[]
    for i in (0,1,2):
        with np.load(ROOT/f'projects/handwriting_synthesis/model/sample_{i}.npz') as data:
            samples.append(data['quantized'])
    fixture=out/'trajectory.txt'
    points=np.concatenate(samples)
    fixture.write_text(''.join(f'{x:.9g} {y:.9g} {p:.0f} {i//20} 0\n' for i,(x,y,p) in enumerate(points)))
    includes=[ROOT/p for p in ('tests/handwriting','projects/handwriting_synthesis',
        'projects/handwriting_synthesis/generated','projects/handwriting_synthesis/nodes_impl',
        'board/DevKit-E8',
        'thirdparty/CMSIS-Stream/platform','thirdparty/CMSIS-Stream/platform/posix_runtime')]
    for name in ('test_handwriting_nodes', 'test_handwriting_networks'):
        binary=out/(name+'.exe')
        sources=[ROOT/'tests'/(name+'.cpp')]
        if name=='test_handwriting_networks':
            sources.append(ROOT/'projects/handwriting_synthesis/nodes_impl/handwriting_engine.cpp')
        if args.vcvars:
            batch=out/'compile.cmd'
            batch.write_text('@echo off\ncall "'+str(args.vcvars.resolve())+'" >nul\ncl /nologo /std:c++17 /EHsc /O2 /fp:fast '
                +' '.join('/I"'+str(p)+'"' for p in includes)+' '
                +' '.join('"'+str(source)+'"' for source in sources)+' /Fe:"'+str(binary)+'"\n')
            subprocess.run(['cmd','/d','/c',str(batch)],cwd=out,check=True)
        else:
            subprocess.run([args.cxx,'-std=c++17','-O2','-ffast-math',*[f'-I{p}' for p in includes],
                            *map(str,sources),'-o',str(binary)],check=True)
        result=subprocess.run([str(binary),str(fixture)],stdout=subprocess.PIPE,check=True)
        if name=='test_handwriting_nodes':
            # Terminal protocol: echo edited input, submit CRLF only once,
            # display normalization, and visibly clear the unfinished line.
            for expected in (
                b'\rab\b \bcd\r\nSentence 1 (3 chars): acd\r\n',
                b'\rqxz\r\nSentence 2 (3 chars): qxz\r\n',
                b'\rHi@\b \b there\r\nSentence 3 (8 chars): Hi there\r\n',
                b'\runfinished'+b'\b \b'*10+b'\rHello\r\nSentence 6 (5 chars): Hello\r\n',
            ):
                assert expected in result.stdout,repr(expected)
            assert b'acd\r\n\r\n' not in result.stdout
        print(result.stdout.decode(errors='replace'),end='')
    raw=Path(str(fixture)+'.ppm').read_bytes().split(b'\n',3)[3]
    def chunk(tag,payload):
        return struct.pack('>I',len(payload))+tag+payload+struct.pack('>I',zlib.crc32(tag+payload)&0xffffffff)
    scan=b''.join(b'\0'+raw[y*2400:(y+1)*2400] for y in range(480))
    (out/'layout-preview.png').write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',800,480,8,2,0,0,0))+
        chunk(b'IDAT',zlib.compress(scan))+chunk(b'IEND',b''))
if __name__=='__main__':main()
