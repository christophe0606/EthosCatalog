# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Compare the256-hop production reverb with independent full FIR convolution."""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import zlib
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
BACKEND = ROOT/'projects/midi_ddsp/nodes_impl/c'
sys.path.insert(0, str(ROOT/'projects/midi_ddsp/dsp'))
from reference_synth import apply_reverb

WRAPPER = r'''
#include "ddsp_reverb256.hpp"
#include <cstring>
#include <vector>
extern "C" __declspec(dllexport) int render256(const float *dry,const float *ir,float *wet,
 unsigned blocks,unsigned learned,unsigned inplace){
 constexpr unsigned Count=midi_ddsp::Reverb256::SpectrumFloats;
 constexpr float Guard=-3e20f;
 std::vector<float> coefficients(Count+16,Guard),history(Count+16,Guard);
 midi_ddsp::Reverb256 reverb;
 float scratch[256];
 reverb.init(coefficients.data()+8,history.data()+8,ir,learned!=0);
 reverb.process256(dry,scratch); // Dirty history/overlap before reinitializing.
 reverb.init(coefficients.data()+8,history.data()+8,ir,learned!=0);
 if(inplace)std::memcpy(wet,dry,blocks*256*sizeof(float));
 for(unsigned b=0;b<blocks;++b)reverb.process256((inplace?wet:dry)+b*256,wet+b*256);
 for(unsigned i=0;i<8;++i)if(coefficients[i]!=Guard||coefficients[Count+8+i]!=Guard||
   history[i]!=Guard||history[Count+8+i]!=Guard)return -1;
 return 0;
}
'''


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--vcvars', type=Path, required=True)
    parser.add_argument('--fixture', type=Path, default=ROOT/'.flow/midi_ddsp/dsp_runtime_fixture')
    parser.add_argument('--output', type=Path, default=ROOT/'out/midi-ddsp-dsp-tests/reverb256_validation.json')
    args = parser.parse_args()
    sources = {name: sha(BACKEND/name) for name in ('ddsp_reverb256.cpp', 'ddsp_reverb256.hpp')}
    output = ROOT/'out/midi-ddsp-dsp-tests'
    output.mkdir(parents=True, exist_ok=True)
    run = Path(tempfile.mkdtemp(prefix='reverb256-', dir=output))
    wrapper, library, batch = run/'wrapper.cpp', run/'reverb256.dll', run/'compile.cmd'
    wrapper.write_text(WRAPPER)
    batch.write_text('@echo off\ncall "'+str(args.vcvars.resolve())+'" >nul\ncl /nologo /std:c++17 /EHsc /W4 /WX /O2 /LD /I"'+str(BACKEND)+'" "'+str(wrapper)+'" "'+str(BACKEND/'ddsp_reverb256.cpp')+'" /link /OUT:"'+str(library)+'"\n')
    subprocess.run(['cmd', '/d', '/c', str(batch)], cwd=run, check=True)
    dll = ctypes.CDLL(str(library))
    array = np.ctypeslib.ndpointer(dtype=np.float32, flags='C_CONTIGUOUS')
    dll.render256.argtypes, dll.render256.restype = [array]*3+[ctypes.c_uint]*3, ctypes.c_int
    def check(dry, impulse, learned, inplace=False, expected=None):
        dry, impulse = (np.ascontiguousarray(v, np.float32) for v in (dry, impulse))
        if not len(dry) or len(dry) % 256 or impulse.shape != (48000,):
            raise ValueError('Expected whole256-sample blocks and48000 taps')
        before = [hashlib.sha256(v.tobytes()).hexdigest() for v in (dry, impulse)]
        storage = np.full(len(dry)+16, np.float32(1234567.25))
        actual = storage[8:-8]
        if dll.render256(dry, impulse, actual, len(dry)//256, int(learned), int(inplace)) != 0:
            raise AssertionError('Coefficient/history guard overwritten')
        assert np.all(storage[:8] == np.float32(1234567.25)) and np.all(storage[-8:] == np.float32(1234567.25))
        assert before == [hashlib.sha256(v.tobytes()).hexdigest() for v in (dry, impulse)]
        if expected is None:
            if learned:
                expected = apply_reverb(dry, impulse)
            else:
                size = 1 << (len(dry)+len(impulse)-2).bit_length()
                expected = (dry+np.fft.irfft(np.fft.rfft(dry, size)*np.fft.rfft(impulse, size), size)[:len(dry)]).astype(np.float32)
        error = actual.astype(np.float64)-expected.astype(np.float64)
        result = {'samples': len(dry), 'max_abs': float(np.max(np.abs(error))),
                  'rmse': float(np.sqrt(np.mean(error*error))), 'finite': bool(np.isfinite(actual).all()),
                  'learned_init': learned, 'inplace': inplace, 'tolerance': 1e-5}
        if not result['finite'] or result['max_abs'] >= result['tolerance']:
            raise AssertionError(result)
        return result
    fixture_manifest = json.loads((args.fixture/'manifest.json').read_text())
    def read(name):
        path = args.fixture/(name+'.f32')
        if sha(path) != fixture_manifest['files'][path.name]['sha256']:
            raise ValueError('Stale DSP fixture')
        return np.fromfile(path, '<f4')
    learned_fixture = check(read('dry_precise'), read('reverb_impulse'), False, expected=read('wet_precise'))
    # Includes partition transitions255/256 and a final47999-sample onset.
    # Its complete FIR tail ends95998, before the96256-sample observation ends.
    dry = np.zeros(96256, np.float32)
    dry[[0, 1, 255, 256, 257, 47999]] = [1., -.3, .2, .5, -.4, -.7]
    sparse = np.zeros(48000, np.float32)
    sparse[[0, 1, 63, 64, 255, 256, 257, 15999, 16000, 16001, 47999]] = [1., .2, -.3, .4, -.5, .6, -.7, .8, -.9, 1., -.2]
    sparse_results = [check(dry, sparse, learned, inplace) for learned in (False, True) for inplace in (False, True)]
    rng = np.random.Generator(np.random.PCG64(20261104))
    lengths = []
    for blocks in (1, 2, 3, 17, 188, 189, 250):
        signal = rng.uniform(-.1, .1, blocks*256).astype(np.float32)
        lengths.append(check(signal, sparse, True))
    asset_path = ROOT/'projects/midi_ddsp/model/cpu_assets.bin'
    asset = asset_path.read_bytes()
    metadata = json.loads(asset_path.with_suffix('.json').read_text())
    if len(asset) != metadata['bytes'] or zlib.crc32(asset) != metadata['crc32']:
        raise ValueError('CPU asset CRC/size mismatch')
    record = metadata['tensors']['s/reverb_module/magnitudes_embedding/embeddings']
    impulses = np.frombuffer(asset, '<f4', count=record['count'], offset=record['offset']).reshape(13, 48000)
    instruments = []
    for instrument, impulse in enumerate(impulses):
        instruments.append({'instrument_id': instrument, 'metrics': check(dry, impulse, True)})
        print('Reverb256 complete signed learned tail', instrument, 'PASS', flush=True)
    if sources != {name: sha(BACKEND/name) for name in sources}:
        raise ValueError('Production reverb source changed during validation')
    report = {'passed': True, 'hop_samples': 256, 'fft_samples': 512, 'partitions': 188,
              'coefficient_bytes': 385024, 'history_bytes': 385024,
              'extra_accumulation_latency_samples': 192, 'extra_accumulation_latency_ms': 12,
              'learned_fixture': learned_fixture, 'sparse_signed_tails': sparse_results,
              'lengths': lengths, 'all_13_learned_tails': instruments,
              'file_sha256': {**sources, 'test': sha(Path(__file__)), 'cpu_assets.bin': sha(asset_path),
                              'reference_synth.py': sha(ROOT/'projects/midi_ddsp/dsp/reference_synth.py')},
              'scope': 'Host FFT comparison with independent full convolution. Includes full final partial partition, reset, inplace/disjoint use and guards. CMSIS FFT/board timing/audio playback remain unmeasured.'}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2)+'\n')
    print('Reverb256 validation PASS:', args.output, flush=True)


if __name__ == '__main__':
    main()
