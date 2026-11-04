# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Generate sparse target DSP test probes, never a playback audio source."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent/'dsp'))
from reference_synth import (HOP, apply_reverb, filtered_noise, harmonic_audio,
                            reverb_impulse, scale_controls)

FRAMES = 1065
RAW_SEED, WHITE_SEED = 83217, 19100
SELECTED = (0, 1, 16, 17, 749, 750, 998, 999, 1000, 1064)
IR_INDICES = (0, 1, 63, 64, 15999, 16000, 16001, 47999)
IR_VALUES = (1., .2, -.3, .4, -.5, .6, -.7, .8)


def sequence(seed, count):
    values = np.empty(count, np.float32)
    for i in range(count):
        seed = (1664525*seed+1013904223) & 0xffffffff
        # Signed nine-bit dyadic samples are exact across host/target compilers.
        values[i] = np.float32((seed >> 23)-256)*np.float32(1/256)
    return values


def fixture():
    raw = sequence(RAW_SEED, FRAMES*126).reshape(FRAMES, 126)
    white = sequence(WHITE_SEED, FRAMES*HOP)
    hz = np.array([0., 55., 110., 220., 440., 7999., 8000., 9000.], np.float32)[(np.arange(FRAMES)//17) % 8]
    hz[0], hz[999], hz[1000] = 110., 110., 440.
    learned = np.zeros(48000, np.float32)
    learned[list(IR_INDICES)] = IR_VALUES
    amplitude, distribution, magnitudes = scale_controls(raw, hz)
    harmonic = harmonic_audio(amplitude, distribution, hz)
    noise = filtered_noise(magnitudes, white)
    dry = harmonic+noise
    return raw, hz, white, learned, [harmonic, noise, dry, apply_reverb(dry, learned)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', type=Path, default=HERE)
    args = parser.parse_args()
    raw, hz, white, learned, expected = fixture()
    selected = np.stack([np.stack([value[f*HOP:(f+1)*HOP] for value in expected]) for f in SELECTED])
    lines = ['// Copyright 2026 Arm Limited and/or its affiliates.',
             '// SPDX-License-Identifier: Apache-2.0', '#pragma once', '#include <cstdint>',
             '// TEST COMPARISONS ONLY. Never feed expected samples to SAI/playback.',
             '// Generate actual audio using Synth/Reverb and the deterministic inputs below.',
             f'static constexpr unsigned midi_dsp_probe_frames={FRAMES};',
             f'static constexpr uint32_t midi_dsp_probe_raw_seed={RAW_SEED}U;',
             f'static constexpr uint32_t midi_dsp_probe_white_seed={WHITE_SEED}U;',
             f'static constexpr float midi_dsp_probe_tolerance=1e-5f;',
             'static const unsigned midi_dsp_probe_selected[]={'+','.join(map(str, SELECTED))+'};',
             'static const unsigned midi_dsp_probe_ir_indices[]={'+','.join(map(str, IR_INDICES))+'};',
             'static const float midi_dsp_probe_ir_values[]={'+','.join(f'{float(np.float32(v)):.9e}f' for v in IR_VALUES)+'};',
             'inline float midi_dsp_probe_random(uint32_t &state){state=1664525U*state+1013904223U;return float(int(state>>23)-256)*0x1p-8f;}',
             'inline void midi_dsp_probe_fill_raw(uint32_t &state,float *raw){for(unsigned j=0;j<126;++j)raw[j]=midi_dsp_probe_random(state);}',
             'inline void midi_dsp_probe_fill_white(uint32_t &state,float *white){for(unsigned j=0;j<64;++j)white[j]=midi_dsp_probe_random(state);}',
             'inline float midi_dsp_probe_hz(unsigned frame){static const float hz[]={0,55,110,220,440,7999,8000,9000};if(frame==0||frame==999)return 110;if(frame==1000)return 440;return hz[(frame/17)%8];}',
             '// Dimensions [selected control interval][harmonic,noise,dry,wet][64 samples].',
             'static const float midi_dsp_probe_expected[][4][64]={']
    for item in selected:
        lines.append('{'+','.join('{'+','.join(f'{float(v):.9e}f' for v in samples)+'}' for samples in item)+'},')
    lines.append('};')
    args.output_dir.mkdir(parents=True, exist_ok=True)
    header = args.output_dir/'dsp_probe.h'
    header.write_text('\n'.join(lines)+'\n', encoding='utf-8')
    data = args.output_dir/'dsp_probe.npz'
    np.savez_compressed(data, raw=raw, hz=hz, white=white, learned_ir=learned,
                        causal_ir=reverb_impulse(learned), selected=selected)
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    report = {'frames': FRAMES, 'sample_rate_hz': 16000, 'raw_seed': RAW_SEED, 'white_seed': WHITE_SEED,
              'generator': 'LCG32 state=1664525*state+1013904223; float32(int(state>>23)-256)/256',
              'selected_intervals': SELECTED, 'component_order': ['harmonic', 'noise', 'dry', 'wet'],
              'absolute_tolerance': 1e-5, 'phase': 'precise oracle, persistent through1065 intervals',
              'raw_ir_nonzero_indices': IR_INDICES, 'raw_ir_values': IR_VALUES,
              'scope': 'Synthetic deterministic target numeric tests only; selected expected samples are comparison data, never playback input. Verifies FFT/filter/oscillator/reverb state including history wrap. No real-time or musical-quality claim.',
              'file_sha256': {'generator': sha(Path(__file__)), 'reference_synth.py': sha(HERE.parent/'dsp/reference_synth.py'),
                              header.name: sha(header), data.name: sha(data)}}
    (args.output_dir/'dsp_probe.json').write_text(json.dumps(report, indent=2)+'\n')
    print('Target DSP probes:', len(SELECTED), 'intervals,', selected.size, 'comparison samples;', header, flush=True)


if __name__ == '__main__':
    main()
