# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Host DSP validation data from real learned controls; never demo audio assets."""
import argparse
import hashlib
import json
from pathlib import Path
import zlib
import numpy as np
from reference_synth import (HOP, apply_reverb, filtered_noise, harmonic_audio,
                             midi_to_hz, reverb_impulse, scale_controls)

ROOT = Path(__file__).resolve().parents[3]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--controls', type=Path, default=ROOT/'.flow/midi_ddsp/schedule_fixture/schedule_controls.npz')
    parser.add_argument('--cpu-assets', type=Path, default=ROOT/'projects/midi_ddsp/model/cpu_assets.bin')
    parser.add_argument('--output-dir', type=Path, default=ROOT/'.flow/midi_ddsp/dsp_runtime_fixture')
    parser.add_argument('--instrument', type=int, default=0)
    args = parser.parse_args()
    if not 0 <= args.instrument < 13: raise ValueError('Expected instrument ID0..12')
    # The standard LCG fixture is instrument0. Other explicit inputs must carry
    # matching instrument identity in their adjacent trace manifest.
    trace = json.loads(args.controls.with_name('schedule_fixture.json').read_text())
    if trace['instrument_id'] != args.instrument: raise ValueError('Control/instrument mismatch')
    arrays = np.load(args.controls)
    raw = np.asarray(arrays['raw'],np.float32).reshape(-1,126)
    f0 = midi_to_hz(arrays['pitch'].reshape(-1))
    white = np.random.Generator(np.random.PCG64(19100)).uniform(-1,1,len(raw)*HOP).astype(np.float32)
    asset = args.cpu_assets.read_bytes()
    manifest = json.loads(args.cpu_assets.with_suffix('.json').read_text())
    if len(asset)!=manifest['bytes'] or zlib.crc32(asset)!=manifest['crc32']:
        raise ValueError('CPU asset CRC/size mismatch')
    record = manifest['tensors']['s/reverb_module/magnitudes_embedding/embeddings']
    learned = np.frombuffer(asset,dtype='<f4',count=record['count'],offset=record['offset']).reshape(13,48000)[args.instrument]
    amp,distribution,magnitudes = scale_controls(raw,f0)
    harmonic = harmonic_audio(amp,distribution,f0)
    original_phase = harmonic_audio(amp,distribution,f0,phase_mode='ddsp_float32')
    noise = filtered_noise(magnitudes,white)
    dry = harmonic+noise
    source_dry = original_phase+noise
    values = {'raw':raw,'f0_hz':f0,'white_noise':white,
              'harmonic_precise':harmonic,'harmonic_ddsp_float32':original_phase,
              'filtered_noise':noise,'dry_precise':dry,'dry_ddsp_float32':source_dry,
              'reverb_impulse':reverb_impulse(learned),
              'wet_precise':apply_reverb(dry,learned),
              'wet_ddsp_float32':apply_reverb(source_dry,learned)}
    args.output_dir.mkdir(parents=True,exist_ok=True)
    files = {}
    for name,value in values.items():
        data = np.asarray(value,dtype='<f4')
        if not np.isfinite(data).all(): raise ValueError(f'Nonfinite{name}')
        path = args.output_dir/(name+'.f32')
        path.write_bytes(data.tobytes())
        files[path.name] = {'shape':list(data.shape),'bytes':data.nbytes,
                            'sha256':hashlib.sha256(path.read_bytes()).hexdigest()}
    report = {'instrument_id':args.instrument,'frames':len(raw),'sample_rate_hz':16000,
              'dtype':'little-endian float32','white_noise_rng':'NumPy PCG64 seed19100 uniform[-1,1)', 'files':files,
              'source_sha256':{'controls':hashlib.sha256(args.controls.read_bytes()).hexdigest(),
                               'cpu_assets':hashlib.sha256(asset).hexdigest(),
                               'reference_synth.py':hashlib.sha256(Path(__file__).with_name('reference_synth.py').read_bytes()).hexdigest(),
                               'generator':hashlib.sha256(Path(__file__).read_bytes()).hexdigest()},
              'scope':'Host validation fixtures only. Never link expected waveforms or white-noise arrays into demo firmware. Firmware must generate audio incrementally from learned controls and persistent DSP state.'}
    (args.output_dir/'manifest.json').write_text(json.dumps(report,indent=2)+'\n')
    print('DSP runtime validation fixture:',len(raw),'frames,',len(dry),'samples,',len(files),'arrays',flush=True)


if __name__=='__main__': main()
