# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Summarize saved CMSIS observations; never communicates with the board."""
import copy
import json
import re
import statistics
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parent
METHODS = ['expression_forward', 'expression_backward', 'expression_step',
           'precondition', 'context_forward', 'context_backward', 'pitch_step', 'harmonic']
HZ = 400_000_000


def text(item):
    return '\n'.join(block.get('text', '') for block in item['result']['content'])


def field(value, name):
    match = re.search(r'(?m)^\s*' + re.escape(name) + r': ([\d.eE+\-]+)\s*\(', value)
    if not match:
        raise ValueError(f'Missing field {name}')
    return float(match[1]) if any(c in match[1] for c in '.eE') else int(match[1])


def array(value, name):
    part = value.split(f'  {name}: ', 1)[1]
    part = re.split(r'\n  [a-zA-Z_]+:', part, maxsplit=1)[0]
    return [float(m[1]) for m in re.finditer(r'\[\d+\]: ([\d.eE+\-]+)\s*\(', part)]


def summarize():
    first = json.loads((ROOT / 'benchmark_batch16_first.json').read_text())
    entries = {item['expression']: text(item) for item in first['results']}
    methods = []
    for index, name in enumerate(METHODS):
        timing = entries[f'midi_schedule[0].method[{index}]']
        staging = entries[f'midi_schedule[0].staging[{index}]']
        quant = entries[f'midi_schedule[0].quantization[{index}]']
        norm = entries[f'midi_schedule[0].normalization[{index}]']
        cache = entries[f'midi_schedule[0].cache[{index}]']
        wall, cycles = field(timing, 'ticks'), field(timing, 'cycles')
        assert cycles == field(timing, 'active') and not field(timing, 'overflow')
        assert not field(staging, 'constant_bypasses') and not field(staging, 'crc_failures')
        q, dq = array(quant, 'ticks')
        normal = sum(array(norm, 'ticks'))
        cache_ticks = sum(array(cache, 'ticks'))
        methods.append(dict(name=name, wall_ms=wall / HZ * 1000,
            npu_cycles=cycles, npu_ms=cycles / HZ * 1000,
            invocations=field(timing, 'invocations'),
            prepare_ms=field(staging, 'prepare_ticks') / HZ * 1000,
            memcpy_ms=field(staging, 'memcpy_ticks') / HZ * 1000,
            crc_ms=field(staging, 'crc_ticks') / HZ * 1000,
            quantize_ms=q / HZ * 1000, dequantize_ms=dq / HZ * 1000,
            normalization_ms=normal / HZ * 1000, cache_callbacks_ms=cache_ticks / HZ * 1000,
            other_within_method_ms=(wall-cycles-q-dq-normal-cache_ticks) / HZ * 1000,
            method_peak_bytes=field(staging, 'method_peak'), temp_peak_bytes=field(staging, 'temp_peak')))
    schedule = entries['midi_schedule[0]']
    first_summary = dict(total_ticks=field(schedule, 'total_ticks'),
        forward_ms=field(schedule, 'total_ticks') / HZ * 1000,
        caller_outside_methods_ms=field(schedule, 'cpu_ticks') / HZ * 1000,
        npu_cycles=sum(m['npu_cycles'] for m in methods),
        invocations=sum(m['invocations'] for m in methods), methods=methods)
    assert first_summary['invocations'] == 4424
    sweep = json.loads((ROOT / 'benchmark_batch16_sweep.json').read_text())
    checks = json.loads((ROOT / 'benchmark_batch16_sweep_checks.json').read_text())
    for item in checks['results']:
        expression, value = item['expression'], text(item)
        if expression.startswith('midi_stage_stats['):
            assert not field(value, 'constant_bypasses') and not field(value, 'crc_failures')
            assert field(value, 'loads') == 16
        if expression.endswith('.calls'):
            counts = [int(m[1]) for m in re.finditer(r'\[\d+\]: (\d+)\s*\(', value)]
            assert counts == [4,4,4,63,1000,1000,1000,1]
    for item in sweep['results']:
        value = text(item)
        cycles = [int(m[1]) for m in re.finditer(r'(?m)^\s+cycles: (\d+)', value)]
        active = [int(m[1]) for m in re.finditer(r'(?m)^\s+active: (\d+)', value)]
        invocations = [int(m[1]) for m in re.finditer(r'(?m)^\s+invocations: (\d+)', value)]
        overflows = [int(m[1]) for m in re.finditer(r'(?m)^\s+overflow: (\d+)', value)]
        assert len(cycles) == 8 and cycles == active and sum(invocations) == 4424
        assert len(overflows) == 8 and not any(overflows)
        assert field(value, 'complete') == field(value, 'finite_outputs') == 1
    observations = sweep['summaries']
    repeats = [s['total_ticks'] for s in observations[:3]]
    held_source = json.loads((ROOT / 'benchmark_batch16_held_timings.json').read_text())
    held = []
    for index, item in enumerate(held_source['results']):
        value = text(item)
        samples = [int(m[1]) for m in re.finditer(r'(?m)^\s+ticks: (\d+)', value)]
        assert len(samples) == 4
        held.append(dict(name=METHODS[index], first_use_ms=samples[0]/HZ*1000,
            warm_median_ms=statistics.median(samples[1:])/HZ*1000,
            max_ms=max(samples)/HZ*1000, ticks=samples))
    result = dict(pte_sha256=first['pte_sha256'], timer_hz=HZ,
        first=first_summary, held=held, sweep=observations,
        instrument0_repeat_median_ms=statistics.median(repeats)/HZ*1000,
        sweep_min_ms=min(s['total_ticks'] for s in observations)/HZ*1000,
        sweep_max_ms=max(s['total_ticks'] for s in observations)/HZ*1000,
        observed_max_ms=max(first_summary['forward_ms'], max(s['total_ticks'] for s in observations)/HZ*1000),
        accuracy='Provisional harmonic max-error gate FAIL; coupled audio and seams unaccepted.',
        startup='Third observed first-held InvalidProgram35 failure, origin unresolved; successful restarted sweep excluded that failed launch.')
    (ROOT / 'benchmark_batch16_summary.json').write_text(json.dumps(result, indent=2)+'\n')
    return result


def update_benchmark(summary):
    path = ROOT / 'benchmark.yml'
    document = yaml.safe_load(path.read_text())
    records = document['benchmarks']['benchmark']
    records[:] = [r for r in records if r['run'].get('model_sha256') != summary['pte_sha256']]
    template = records[-1]
    first = summary['first']
    sets = [('b-50', 'd-51', [dict(total_ticks=first['total_ticks'], npu_cycles=first['npu_cycles'])],
             'First fresh launch; four held calls per method preceded instrument0 full schedule. '),
            ('b-52', 'd-53 same-ELF restart', summary['sweep'],
             'Fifteen full schedules: instrument0 repeated three times, then identities1 through12 once each. ')]
    for build, execution, samples, scope in sets:
        record = copy.deepcopy(template)
        memory = record['measurements']['memory']
        memory['model_bytes'] = 6_221_424
        memory['executorch']['method_pool_peak_bytes'] = 190_180
        memory['executorch']['temp_pool_peak_bytes'] = 1_216_000
        cycles = [s['npu_cycles'] for s in samples]
        ticks = [s['total_ticks'] for s in samples]
        record['measurements']['time'] = dict(ms=statistics.mean(cycles)/HZ*1000,
            cycles=statistics.mean(cycles), statistic='mean', min_cycles=min(cycles),
            max_cycles=max(cycles), mean_cycles=statistics.mean(cycles),
            forward_ms=statistics.mean(ticks)/HZ*1000, forward_timer_hz=HZ,
            cycles_samples=cycles, forward_ticks_samples=ticks)
        run = record['run']
        run.update(recorded_at='2026-11-04T14:34:45Z', build_job=build, execution_job=execution,
            samples=len(samples), warmup=0, npu_invocations_per_forward=4424,
            pmu_cross_check='CYCLE equals NPU_ACTIVE on every invocation; zero overflow; 4424 actual U85 invocations per complete block.',
            model_sha256=summary['pte_sha256'], test_result='FAIL',
            max_absolute_error=0.0286214948, tolerance=0.0119089603,
            input='Synthetic four-region score: notes48+identity,55+identity,rest,60+identity each250frames; LCGseed9100+identity. All13 identities are timing coverage; actual demo registers and audio quality are separate acceptance work. Held harmonic max0.0286214948 exceeds unchanged provisional0.0119089603 gate; NRMSE0.164406percent. Instrument0 pitchMAE0.028380014semitones and selectedrawmax0.0163551569 match batch1.',
            timing_scope=scope+'Complete 3076-method schedule: precondition63x16 rows,1000valid+8discarded padding; other learned methods unchanged. Includes stage loading/copy/tableCRC/teardown, CPU embeddings, state/context/output copies, top-p sampling, finite checks, statistics and profiling overhead. Six normalization overrides, fast MVE quantization, native dequantization; SRAM-staged recurrent delegates and cached CPU constants, direct-OSPI harmonic. Startup CRC and held calls excluded from forward time; their first-use/warm timings are separate. No DSP, PCM, SAI, LCD or playback included.',
            measurement_source='CMSIS '+build+'/'+execution+'; benchmark_batch16_first.json, benchmark_batch16_held_timings.json, benchmark_batch16_sweep.json, benchmark_batch16_sweep_checks.json and benchmark_batch16_summary.json. All asset/segment CRCs pass; no constant bypass, no normalization fallback,717target edgecases pass. All complete schedules finite and faster than4000ms. Test_result remains FAIL for unchanged provisional harmonic gate. One initial b52/d53 first-held Error35 launch was excluded and recorded separately; its origin remains unresolved. No final audio/voice-cap claim.')
        records.append(record)
    path.write_text(yaml.safe_dump(document, sort_keys=False, width=90))


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--update-benchmark', action='store_true')
    args = parser.parse_args()
    summary = summarize()
    if args.update_benchmark:
        update_benchmark(summary)
    print(json.dumps(summary, indent=2))
