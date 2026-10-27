#!/usr/bin/env python3
# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Host-check the shipped packed vocabulary, frontend bounds and LPC output.

Requires a native clang on PATH. Build products stay in repository tmp/.
"""
import ctypes as C
import json
from pathlib import Path
import shutil
import struct
import subprocess
import zlib

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]

def vocabulary(blob):
    _,count,buckets,size,index,entries,_,_=struct.unpack_from('<8I',blob)
    for bucket in range(buckets):
        cursor=entries+struct.unpack_from('<I',blob,index+bucket*4)[0]
        word=b''
        for _ in range(min(size,count-bucket*size)):
            h=struct.unpack_from('<H',blob,cursor)[0];cursor+=2
            shared,length=h&31,(h>>5)&31
            word=word[:shared]+blob[cursor:cursor+length-shared];cursor+=length-shared
            identifier=struct.unpack_from('<H',blob,cursor)[0];cursor+=2
            yield word,identifier

def main():
    build=ROOT/'tmp/rvtts-runtime';build.mkdir(parents=True,exist_ok=True)
    wrapper=build/'reference.c'
    (build/'benchmark_inputs.h').write_text((HERE/'model/benchmark_inputs.h').read_text().replace('#include <cstdint>','#include <stdint.h>'))
    wrapper.write_text('''#include <stdint.h>
#include "tts_frontend.h"
#include "pron_dict.h"
#include "word_vocab_packed.h"
#include "lpcvoc.h"
#include "benchmark_inputs.h"
__declspec(dllexport) int init(const void*d,unsigned nd,const void*w,unsigned nw){return pron_dict_init(d,nd)||word_vocab_init(w,nw);}
__declspec(dllexport) unsigned pronunciation(const char*w,uint8_t*out){const PronEntry*p=pron_lookup(w);if(!p)return 0;for(unsigned i=0;i<p->phone_count;++i)out[i]=p->phones[i]|(p->stress[i]<<6);return p->phone_count;}
__declspec(dllexport) unsigned lookup(const char*w){return word_vocab_lookup(w);}
__declspec(dllexport) int frontend(const char*t,unsigned*frames){static TtsFrontendResult r;int e=tts_frontend_process(t,&r,0);*frames=(unsigned)r.frame_count;for(unsigned i=0;i<r.frame_count;++i)if(r.frame_phones[i]>=41||r.frame_stress[i]>=4)return -99;return e;}
__declspec(dllexport) void vocoder(int16_t*pcm){LpcVocState s;lpcvoc_init(&s);for(unsigned t=0;t<157;++t)lpcvoc_frame(&s,rvtts_sequence_157_features+20*t,pcm+160*t);}
''')
    compiler=shutil.which('clang')
    if not compiler: raise RuntimeError('Native clang required on PATH')
    sources=['tts_frontend.c','textnorm.c','duration_model.c','reciter.c','word_vocab_packed.c','lpcvoc.c']
    library=build/'runtime.dll'
    subprocess.run([compiler,'-shared','-O2','-D_CRT_SECURE_NO_WARNINGS','-I'+str(HERE/'upstream'),'-I'+str(HERE/'model/upstream'),'-I'+str(HERE/'model'),'-o',str(library),str(wrapper),str(HERE/'nodes_impl/pron_dictionary.c'),*[str(HERE/'upstream'/s) for s in sources]],check=True,cwd=build)
    lib=C.CDLL(str(library));lib.init.argtypes=[C.c_void_p,C.c_uint,C.c_void_p,C.c_uint];lib.lookup.argtypes=[C.c_char_p];lib.pronunciation.argtypes=[C.c_char_p,C.c_void_p];lib.frontend.argtypes=[C.c_char_p,C.POINTER(C.c_uint)];lib.vocoder.argtypes=[C.c_void_p]
    dictionary=C.create_string_buffer((HERE/'model/upstream/PRONDICT.bin').read_bytes())
    words=(HERE/'model/upstream/WORDVOC.bin').read_bytes();word_buffer=C.create_string_buffer(words)
    assert lib.init(dictionary,len(dictionary)-1,word_buffer,len(words))==0
    entries=list(vocabulary(words))
    mapping=dict(entries)
    for word,identifier in entries:
        normalized=bytes(c for c in word.lower() if 97<=c<=122 or 48<=c<=57 or c in (39,45)).rstrip(b"'-")
        expected=mapping.get(normalized,0)
        assert lib.lookup(word)==expected,(word,expected)
    dictionary_blob=bytes(dictionary)[:-1]
    _,count,buckets,size,index,entries,_,_=struct.unpack_from('<8I',dictionary_blob)
    pronunciation=(C.c_uint8*32)()
    for bucket in range(buckets):
        cursor=entries+struct.unpack_from('<I',dictionary_blob,index+4*bucket)[0]
        word=b''
        for row in range(min(size,count-bucket*size)):
            h=struct.unpack_from('<H',dictionary_blob,cursor)[0];cursor+=2
            shared,length,phones=h&31,(h>>5)&31,(h>>10)&31
            if row==0: assert shared==0
            word=word[:shared]+dictionary_blob[cursor:cursor+length-shared];cursor+=length-shared
            reference=dictionary_blob[cursor:cursor+phones];cursor+=phones
            actual=lib.pronunciation(word,pronunciation)
            assert actual==phones and bytes(pronunciation)[:actual]==reference,word
    fixtures={}
    for text in ['Hello.','The meter is reading forty five.','One two three.','This sentence is deliberately long enough to exceed the bounded inference context and must be rejected by the application.']:
        frames=C.c_uint();assert lib.frontend(text.encode(),C.byref(frames))==0
        fixtures[text]=frames.value
    assert fixtures['Hello.']<=224
    assert list(fixtures.values())[-1]>224
    pcm=(C.c_int16*(157*160))();again=(C.c_int16*(157*160))()
    lib.vocoder(pcm);lib.vocoder(again);assert bytes(pcm)==bytes(again)
    assert max(abs(v) for v in pcm)>0
    report=dict(vocabulary_entries=len(mapping),dictionary_entries=count,frontend_frames=fixtures,
                source_vocoder_samples=len(pcm),pcm_crc32=f'{zlib.crc32(bytes(pcm)):08x}',pcm_peak=max(abs(v) for v in pcm))
    (HERE/'runtime_validation.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))

if __name__=='__main__':main()


