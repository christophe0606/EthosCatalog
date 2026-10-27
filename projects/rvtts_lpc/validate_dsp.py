#!/usr/bin/env python3
# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Compare complete optimized LPC utterances with preserved rvTTS scalar C.

Pass the installed CMSIS-DSP release directory with --dsp-root. The host uses
the release's scalar dot; startup checks also compare its MVE path on board.
"""
import argparse
import ctypes as C
import json
from pathlib import Path
import shutil
import subprocess
import numpy as np

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dsp-root', type=Path, required=True)
    args = parser.parse_args()
    dsp = args.dsp_root.resolve()
    build = ROOT/'tmp/rvtts-lpc-dsp'
    build.mkdir(parents=True, exist_ok=True)
    compiler = shutil.which('clang')
    if not compiler:
        raise RuntimeError('Native clang is required on PATH')
    (build/'benchmark_inputs.h').write_text(
        (HERE/'model/benchmark_inputs.h').read_text().replace('#include <cstdint>', '#include <stdint.h>'))
    wrapper = build/'wrapper.c'
    wrapper.write_text('''#include "lpcvoc.h"
#include "benchmark_inputs.h"
void lpcvoc_reference_init(LpcVocState*);
void lpcvoc_reference_frame(LpcVocState*,const float*,int16_t*);
__declspec(dllexport) int synth(int optimized,int fixture,int repeats,int mode,int16_t *out){
    const unsigned counts[]={2,81,157};
    const float *data[]={rvtts_sequence_2_features,rvtts_sequence_81_features,rvtts_sequence_157_features};
    LpcVocState state;
    if(optimized)lpcvoc_init(&state);else lpcvoc_reference_init(&state);
    unsigned n=counts[fixture];
    for(unsigned t=0;t<n*(unsigned)repeats;++t){
        float f[20];for(unsigned j=0;j<20;++j)f[j]=data[fixture][(t%n)*20+j];
        if(mode){f[18]=mode==1?1.5f:-1.494353f;f[19]=(t%53<26)?-.1f:.35f;}
        if(optimized)lpcvoc_frame(&state,f,out+t*160);else lpcvoc_reference_frame(&state,f,out+t*160);
    }
    return n*repeats*160;
}
''')
    libpath = build/'lpc_dsp.dll'
    subprocess.run([compiler, '-shared', '-O3', '-ffast-math', '-D__GNUC_PYTHON__',
                    '-I'+str(HERE/'upstream'), '-I'+str(dsp/'Include'), '-I'+str(dsp/'PrivateInclude'),
                    str(wrapper), str(HERE/'dsp/lpcvoc_reference.c'), str(HERE/'dsp/lpcvoc_optimized.c'),
                    str(dsp/'Source/BasicMathFunctions/arm_dot_prod_f32.c'), '-o', str(libpath)],
                   check=True, cwd=build)
    lib = C.CDLL(str(libpath))
    lib.synth.argtypes = [C.c_int, C.c_int, C.c_int, C.c_int, C.c_void_p]
    cases = []
    for fixture, frames in enumerate([2, 81, 157]):
        for repeats, mode in [(1, 0), (8, 0), (4, 1), (4, 2)]:
            n = frames*repeats*160
            reference = np.zeros(n, np.int16)
            optimized = np.zeros(n, np.int16)
            assert lib.synth(0, fixture, repeats, mode, reference.ctypes.data) == n
            assert lib.synth(1, fixture, repeats, mode, optimized.ctypes.data) == n
            error = reference.astype(np.float64)-optimized.astype(np.float64)
            energy = float(np.dot(reference.astype(np.float64), reference.astype(np.float64)))
            noise = float(np.dot(error, error))
            snr = float(10*np.log10(energy/noise)) if noise else None
            peak = int(np.abs(error).max())
            cases.append(dict(frames=frames*repeats, pitch_mode=mode, samples=n,
                              max_pcm_error=peak, pcm_snr_db=snr, rmse=float(np.sqrt(noise/n))))
            assert peak <= 64 and (snr is None or snr >= 60), cases[-1]
    report = dict(scope='Native scalar CMSIS-DSP dot and sparse FIR versus preserved scalar source; MVE checked separately on board',
                  cmsis_dsp_release=dsp.name, acceptance=dict(max_pcm_error=64, minimum_pcm_snr_db=60), cases=cases)
    (HERE/'dsp/host_validation.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
