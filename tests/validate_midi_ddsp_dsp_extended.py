# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Exercise production DSP state, all13 learned controls and complete signed tails."""
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
from validate_midi_ddsp_dsp import ROOT, BACKEND, WRAPPER

sys.path.insert(0, str(ROOT/'projects/midi_ddsp/dsp'))
from reference_synth import (HOP, apply_reverb, filtered_noise, harmonic_audio,
                            midi_to_hz, reverb_impulse, scale_controls)

REVERB_WRAPPER = r'''
extern "C" __declspec(dllexport) void reverb_only(const float *dry,const float *ir,float *wet,unsigned frames){
 std::vector<float> coefficients(96000),history(96000);
 midi_ddsp::Reverb reverb;reverb.init(coefficients.data(),history.data(),ir,true);
 for(unsigned f=0;f<frames;++f)reverb.process(dry+f*64,wet+f*64);
}
'''


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def check(actual, expected, name):
    if actual.shape != expected.shape:
        raise AssertionError((name, actual.shape, expected.shape))
    error = actual.astype(np.float64)-expected.astype(np.float64)
    result = {'max_abs': float(np.max(np.abs(error))),
              'rmse': float(np.sqrt(np.mean(error*error))),
              'finite': bool(np.all(np.isfinite(actual))), 'tolerance': 1e-5,
              'maximum_error_sample': int(np.argmax(np.abs(error)))}
    if not result['finite'] or result['max_abs'] >= result['tolerance']:
        raise AssertionError((name, result))
    return result


def guarded(size):
    data = np.full(size+16, np.float32(1234567.25))
    return data, data[8:-8]


def assert_guards(data):
    if not np.all(data[:8] == np.float32(1234567.25)) or not np.all(data[-8:] == np.float32(1234567.25)):
        raise AssertionError('Output buffer bounds exceeded')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--vcvars', type=Path, required=True)
    p.add_argument('--fixture', type=Path, default=ROOT/'.flow/midi_ddsp/dsp_runtime_fixture')
    p.add_argument('--controls-dir', type=Path, default=ROOT/'.flow/midi_ddsp/closed_loop_batch16')
    p.add_argument('--output', type=Path, default=ROOT/'out/midi-ddsp-dsp-tests/extended_validation.json')
    args = p.parse_args()
    production_sources = {name: digest(BACKEND/name) for name in ('ddsp_synth.cpp', 'ddsp_synth.hpp')}
    output = ROOT/'out/midi-ddsp-dsp-tests'
    output.mkdir(parents=True, exist_ok=True)
    run = Path(tempfile.mkdtemp(prefix='run-', dir=output))
    source = run/'wrapper.cpp'
    source.write_text(WRAPPER+REVERB_WRAPPER)
    library, batch = run/'dsp.dll', run/'compile.cmd'
    batch.write_text('@echo off\ncall "'+str(args.vcvars.resolve())+'" >nul\ncl /nologo /std:c++17 /EHsc /W4 /WX /O2 /LD /I"'+str(BACKEND)+'" "'+str(source)+'" "'+str(BACKEND/'ddsp_synth.cpp')+'" /link /OUT:"'+str(library)+'"\n')
    subprocess.run(['cmd', '/d', '/c', str(batch)], cwd=run, check=True)
    dll = ctypes.CDLL(str(library))
    array = np.ctypeslib.ndpointer(dtype=np.float32, flags='C_CONTIGUOUS')
    dll.render.argtypes, dll.render.restype = [array]*8+[ctypes.c_uint], None
    dll.reverb_only.argtypes, dll.reverb_only.restype = [array]*3+[ctypes.c_uint], None
    sources = {**production_sources,
               'reference_synth.py': digest(ROOT/'projects/midi_ddsp/dsp/reference_synth.py'),
               'test': digest(Path(__file__)),
               'wrapper': digest(ROOT/'tests/validate_midi_ddsp_dsp.py')}
    manifest = json.loads((args.fixture/'manifest.json').read_text())
    for name, info in manifest['files'].items():
        if digest(args.fixture/name) != info['sha256']:
            raise ValueError(f'Fixture hash mismatch: {name}')
    def read(name):
        return np.fromfile(args.fixture/(name+'.f32'), dtype='<f4')
    def execute(raw, hz, white, ir, expected=None):
        inputs = [np.ascontiguousarray(value, dtype=np.float32).reshape(-1) for value in (raw, hz, white, ir)]
        before = [hashlib.sha256(value.tobytes()).hexdigest() for value in inputs]
        allocations = [guarded(len(white)) for _ in range(4)]
        actual = [item[1] for item in allocations]
        dll.render(*inputs, *actual, len(hz))
        for data, _ in allocations:
            assert_guards(data)
        if before != [hashlib.sha256(value.tobytes()).hexdigest() for value in inputs]:
            raise AssertionError('DSP mutated input controls/noise/IR')
        if expected is None:
            amplitude, distribution, magnitudes = scale_controls(raw, hz)
            harmonic = harmonic_audio(amplitude, distribution, hz)
            noise = filtered_noise(magnitudes, white)
            dry = harmonic+noise
            # Render's ABI takes already decayed/causal taps. The independent
            # convolution therefore uses them directly, with unity dry.
            fft_size = 1 << (len(dry)+len(ir)-2).bit_length()
            wet = (dry+np.fft.irfft(np.fft.rfft(dry, fft_size)*np.fft.rfft(ir, fft_size), fft_size)[:len(dry)]).astype(np.float32)
            expected = [harmonic, noise, dry, wet]
        try:
            return {name: check(a, b, name) for name, a, b in zip(('harmonic', 'noise', 'dry', 'wet'), actual, expected)}
        except AssertionError:
            diagnostic = {'raw': raw, 'hz': hz, 'white': white, 'causal_ir': ir}
            for name, a, b in zip(('harmonic', 'noise', 'dry', 'wet'), actual, expected):
                diagnostic['actual_'+name] = a
                diagnostic['expected_'+name] = b
            failure = run/'failure.npz'
            np.savez_compressed(failure, **diagnostic)
            print('Failure arrays:', failure, flush=True)
            raise
    raw, hz, white, ir = map(read, ['raw', 'f0_hz', 'white_noise', 'reverb_impulse'])
    fixture = execute(raw.reshape(-1, 126), hz, white, ir,
                      [read(name) for name in ('harmonic_precise', 'filtered_noise', 'dry_precise', 'wet_precise')])
    sys.path.insert(0, str(ROOT/'projects/midi_ddsp/model'))
    from make_dsp_probe import fixture as target_fixture
    probe_raw, probe_hz, probe_white, probe_ir, probe_expected = target_fixture()
    target_probe = execute(probe_raw, probe_hz, probe_white, reverb_impulse(probe_ir), probe_expected)
    sources['make_dsp_probe.py'] = digest(ROOT/'projects/midi_ddsp/model/make_dsp_probe.py')
    print('Target probe full-waveform host comparison PASS', flush=True)
    rng = np.random.Generator(np.random.PCG64(20261104))
    boundaries = []
    for frames in (1, 4, 17, 65, 1001, 1065):
        raw = rng.normal(-1, 1, (frames, 126)).astype(np.float32)
        # Zero, negative-MIDI rest residual frequency, low notes, Nyquist
        # crossing and all harmonics pruned, with an abrupt1000-frame seam.
        pattern = np.array([0., 8.17, 55., 110., 440., 7999., 8000., 9000.], np.float32)
        hz = pattern[(np.arange(frames)//17) % len(pattern)]
        # Short finite segments also need a nonzero oscillator, including the
        # single-frame held-endpoint case; later frames retain zero/rest tests.
        hz[0] = 110.
        if frames > 1000:
            hz[999:1001] = [110., 440.]
        white = rng.uniform(-1, 1, frames*HOP).astype(np.float32)
        boundaries.append({'frames': frames, 'metrics': execute(raw, hz, white, ir)})
        print('Boundary DSP case', frames, 'PASS', flush=True)
    asset_path = ROOT/'projects/midi_ddsp/model/cpu_assets.bin'
    asset = asset_path.read_bytes()
    metadata = json.loads(asset_path.with_suffix('.json').read_text())
    if len(asset) != metadata['bytes'] or zlib.crc32(asset) != metadata['crc32']:
        raise ValueError('CPU asset CRC/size mismatch')
    record = metadata['tensors']['s/reverb_module/magnitudes_embedding/embeddings']
    learned = np.frombuffer(asset, dtype='<f4', count=record['count'], offset=record['offset']).reshape(13, 48000)
    sources['cpu_assets.bin'] = digest(asset_path)
    # Finish complete tails: last dry impulse47999 + last IR tap47999 <96256.
    dry = np.zeros(1504*HOP, np.float32)
    dry[[0, 63, 64, 47999]] = [1., -.3, .5, -.7]
    instruments, common_provenance = [], None
    for instrument in range(13):
        trace_path = args.controls_dir/f'instrument_{instrument:02}.json'
        controls_path = args.controls_dir/f'instrument_{instrument:02}_controls.npz'
        trace = json.loads(trace_path.read_text())
        if trace['result']['instrument_id'] != instrument:
            raise ValueError('Instrument/control mismatch')
        if common_provenance is not None and trace['provenance'] != common_provenance:
            raise ValueError('Mixed control corpus provenance')
        common_provenance = trace['provenance']
        for name, value in common_provenance['file_sha256'].items():
            if digest(ROOT/'projects/midi_ddsp/model'/name) != value:
                raise ValueError(f'Stale model/source checkpoint: {name}')
        sources[controls_path.name] = digest(controls_path)
        with np.load(controls_path, allow_pickle=False) as controls:
            raw = np.asarray(controls['ptq_raw'][0], np.float32)
            hz = midi_to_hz(controls['ptq_pitch'].reshape(-1))
        if raw.shape != (len(hz), 126) or not np.isfinite(raw).all():
            raise ValueError('Invalid saved controls')
        white = rng.uniform(-1, 1, len(raw)*HOP).astype(np.float32)
        metrics = execute(raw, hz, white, reverb_impulse(learned[instrument]))
        storage, wet = guarded(len(dry))
        dll.reverb_only(dry, learned[instrument], wet, len(dry)//HOP)
        assert_guards(storage)
        tail = check(wet, apply_reverb(dry, learned[instrument]), 'learned initialization and complete tail')
        instruments.append({'instrument_id': instrument, 'frames': len(raw), 'metrics': metrics, 'learned_init_and_complete_tail': tail})
        print('Learned DSP and complete tail instrument', instrument, 'PASS', flush=True)
    # First tap suppressed, signed taps and decay endpoints across history wrap.
    sparse = np.zeros(48000, np.float32)
    sparse[[0, 1, 63, 64, 15999, 16000, 16001, 47999]] = [1., .2, -.3, .4, -.5, .6, -.7, .8]
    storage, wet = guarded(len(dry))
    dll.reverb_only(dry, sparse, wet, len(dry)//HOP)
    assert_guards(storage)
    sparse_tail = check(wet, apply_reverb(dry, sparse), 'sparse signed tail and first-tap suppression')
    if production_sources != {name: digest(BACKEND/name) for name in production_sources}:
        raise ValueError('Production DSP source changed during compile/validation; rerun')
    report = {'passed': True, 'fixture': fixture, 'target_probe': target_probe, 'boundary_lengths': boundaries,
              'instruments': instruments, 'sparse_signed_complete_tail': sparse_tail,
              'file_sha256': sources, 'control_provenance': common_provenance,
              'scope': 'Production bounded C++ host FFT. Persistent state beyond1000 intervals,750-partition history wrap, output guards and full tails. CMSIS FFT/board timing, firmware buffer handoff, natural demo registers and model-window seams remain separate.'}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2)+'\n')
    print('DSP extended validation PASS:', args.output, flush=True)


if __name__ == '__main__':
    main()
