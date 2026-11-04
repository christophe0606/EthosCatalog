# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Compile the bounded production DSP and compare independent learned fixtures."""
import argparse
import ctypes
import json
from pathlib import Path
import subprocess
import tempfile
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
BACKEND = ROOT / 'projects/midi_ddsp/nodes_impl/c'
WRAPPER = r'''
#include "ddsp_synth.hpp"
#include <vector>
extern "C" __declspec(dllexport) void render(const float *raw,const float *hz,
 const float *white,const float *ir,float *harmonic,float *noise,float *dry,float *wet,unsigned frames){
 midi_ddsp::Synth synth;synth.reset();synth.prime(raw,white);
 std::vector<float> coefficients(96000),history(96000);
 midi_ddsp::Reverb reverb;reverb.init(coefficients.data(),history.data(),ir);
 for(unsigned f=0;f<frames;++f){unsigned next=f+1<frames?f+1:f;
 synth.interval(raw+f*126,raw+next*126,hz[f],hz[next],f+1<frames?white+(f+1)*64:nullptr,harmonic+f*64,noise+f*64,dry+f*64);
 reverb.process(dry+f*64,wet+f*64);}
}
'''

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--vcvars',type=Path,required=True)
    p.add_argument('--fixture',type=Path,default=ROOT/'.flow/midi_ddsp/dsp_runtime_fixture')
    args=p.parse_args()
    output=ROOT/'out/midi-ddsp-dsp-tests';output.mkdir(parents=True,exist_ok=True)
    run=Path(tempfile.mkdtemp(prefix='run-',dir=output));source=run/'wrapper.cpp';source.write_text(WRAPPER)
    library=run/'dsp.dll';batch=run/'compile.cmd'
    batch.write_text('@echo off\ncall "'+str(args.vcvars.resolve())+'" >nul\ncl /nologo /std:c++17 /EHsc /W4 /WX /O2 /LD /I"'+str(BACKEND)+'" "'+str(source)+'" "'+str(BACKEND/'ddsp_synth.cpp')+'" /link /OUT:"'+str(library)+'"\n')
    subprocess.run(['cmd','/d','/c',str(batch)],cwd=run,check=True)
    dll=ctypes.CDLL(str(library));array=np.ctypeslib.ndpointer(dtype=np.float32,flags='C_CONTIGUOUS')
    dll.render.argtypes=[array]*8+[ctypes.c_uint]
    def read(name):return np.fromfile(args.fixture/(name+'.f32'),dtype='<f4')
    raw,hz,white,ir=map(read,['raw','f0_hz','white_noise','reverb_impulse'])
    actual=[np.zeros(len(white),dtype=np.float32) for _ in range(4)]
    dll.render(raw,hz,white,ir,*actual,len(hz))
    report={}
    for result,name in zip(actual,['harmonic_precise','filtered_noise','dry_precise','wet_precise']):
        expected=read(name);error=result-expected
        report[name]={'max_abs':float(np.max(np.abs(error))),'rmse':float(np.sqrt(np.mean(error.astype(float)**2))),'finite':bool(np.all(np.isfinite(result)))}
        assert report[name]['finite'] and report[name]['max_abs']<1e-5,(name,report[name])
    report['scope']='Production bounded C++ with host FFT fallback, four seconds actual learned controls. CMSIS FFT and board speed require target validation.'
    (output/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))

if __name__=='__main__':main()
