#!/usr/bin/env python3
# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Summarize the preserved, uninterrupted CMSIS UART benchmark capture."""
from pathlib import Path
import hashlib
import json
import re
import statistics
import yaml

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]

def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()

def timing(rows,hz):
    cycles=[r[0] for r in rows]; ticks=[r[1] for r in rows]
    return dict(ms=statistics.median(cycles)*1000/hz,cycles=statistics.median(cycles),
                statistic='median',min_cycles=min(cycles),max_cycles=max(cycles),
                mean_cycles=statistics.mean(cycles),forward_ms=statistics.median(ticks)*1000/hz,
                forward_timer_hz=hz,cycles_samples=cycles,forward_ticks_samples=ticks)

def main():
    parts=[]
    for name in ['benchmark_uart_part1.json','benchmark_uart_part2.json']:
        data=json.loads((HERE/name).read_text())
        text='\n'.join(c['text'] for c in data['content'] if c['type']=='text')
        parts.append(text.split('--- text ---\n',1)[1])
    raw=''.join(parts)
    assert raw.count('Handwriting Release benchmark:')==1 and raw.count('Test_result: PASS')==1
    assert 'FAILED' not in raw and 'E:' not in raw
    (HERE/'benchmark_uart.txt').write_text(raw)
    hz=int(re.search(r'timer=(\d+)',raw)[1])
    steps=re.findall(r'STEP sample=(\d+) cycles=(\d+) ticks=(\d+) invocations=(\d+) overflow=(\d+)',raw)
    assert [int(s[0]) for s in steps]==list(range(1,65))
    assert all(int(s[3])==5 and int(s[4])==0 for s in steps)
    rows=[(int(s[1]),int(s[2])) for s in steps]
    methods={}
    for method in ['lstm1','attention','lstm2','lstm3','gmm']:
        sample=re.findall(rf'DENSE {method} sample=(\d+) cycles=(\d+) ticks=(\d+) invocations=(\d+) overflow=(\d+)',raw)
        assert [int(s[0]) for s in sample]==list(range(1,11))
        assert all(int(s[3])==1 and int(s[4])==0 for s in sample)
        region=re.search(rf'REGION {method} weights=(\d+) weights_address=(0x[0-9a-f]+) arena=(\d+) arena_address=(0x[0-9a-f]+) max_error=([\d.]+) tolerance=([\d.]+)',raw)
        methods[method]={'time':timing([(int(s[1]),int(s[2])) for s in sample],hz),
                         'weights_bytes':int(region[1]),'weights_address':region[2],
                         'arena_bytes':int(region[3]),'arena_address':region[4],
                         'max_absolute_error':float(region[5]),'tolerance':float(region[6])}
    intervals=sorted((int(m['weights_address'],16),int(m['weights_address'],16)+m['weights_bytes']) for m in methods.values())
    assert all(b[0]>=a[1] for a,b in zip(intervals,intervals[1:]))
    assert len({m['arena_address'] for m in methods.values()})==1
    weights=sum(m['weights_bytes'] for m in methods.values())
    arena=max(m['arena_bytes'] for m in methods.values())
    (HERE/'benchmark_dense_methods.json').write_text(json.dumps(methods,indent=2)+'\n')
    pools=re.search(r'POOLS method_peak=(\d+) temp_peak=(\d+)',raw)
    model=ROOT/'projects/handwriting_synthesis/ai_layer/model.pte'
    # The application firmware changes after benchmarking. Keep the recorded
    # binary/source provenance rather than hashing a later build as the baseline.
    provenance=json.loads((HERE/'benchmark_provenance.json').read_text())
    assert sha(model)==provenance['model_sha256']
    record={
      'platform':{'os':'RTX5','os_version':'5.9.1','cpu':'Cortex-M55',
        'memory':{'weights':{'type':'OSPI1','clock_mhz':25,
          'clock_source':'board/DevKit-E8/RTE/BSP/AE822FA0E5597LS0_M55_HP/ospi_xip_user.h OSPI_CLOCK_MHZ'},
          'activations':{'type':'SRAM0','clock_mhz':400}}},
      'measurements':{'memory':{'model_bytes':model.stat().st_size,'input_tensor_bytes':12,
        'ethos':{'weights_region_bytes':weights,'arena_region_bytes':arena},
        'executorch':{'method_pool_peak_bytes':int(pools[1]),'temp_pool_peak_bytes':int(pools[2])}},
        'time':timing(rows,hz)},
      'run':{'recorded_at':'2026-10-22T06:35:00Z','context':'handwriting_synthesis.Release+DevKit-E8',
        'target':'DevKit-E8@Handwriting_Release','device':'AE822FA0E5597LS0','core':'M55_HP','npu':'Ethos-U85','macs':256,
        'packs':{'driver':'ARM::CMSIS-Ethos-U@1.0.0',
                 'driver_component':'ARM::Machine Learning:NPU Support:Ethos-U Driver&Generic U85@2.0.0',
                 'board':'AlifSemiconductor::Ensemble@2.2.1','runtime':'PyTorch::ExecuTorch@1.4.1'},
        'compiler':'AC6@6.24.0','optimize':'-O3 -ffast-math','build_job':'b-22','execution_job':'d-23',
        'cpu_clock_mhz':400,'npu_clock_mhz':400,'warmup':1,'samples':64,
        'pmu_cross_check':'PMCCNTR equals PMEVCNTR0 (NPU_ACTIVE) after every dense and full-step measurement',
        'pmu_overflow':0,'npu_invocations_per_forward':5,'test_result':'PASS',
        'uart':'benchmark_uart.txt','model_sha256':provenance['model_sha256'],'firmware_sha256':provenance['firmware_sha256'],
        'input':'75 supported characters plus NUL, seed42, evolving sampled feedback; first step warmup then64 measured steps; per-method held-out dense fixtures10 samples each',
        'timing_scope':'ms/cycles: sum of five real NPU_ACTIVE intervals. forward_ms: complete Engine::step including input assembly, five float-boundary dense calls, output copies, all LSTM gates, full76-position attention/window, mixture/pen sampling, termination check, finite validation and PMU hooks. Excludes UART prints between samples, model load/CRC, method warmup, LCD and UART input.',
        'uart_run':'Uninterrupted explicitly triggered Release benchmark after CMSIS debugger startup settled; no breakpoints or debug reads during measured execution.',
        'measurement_source':'UART4 COM7 115200 8N1; aggregate PMU callbacks, per-method region snapshots, PeakAllocator high-water marks; benchmark_dense_methods.json has separate dense statistics.'}}
    doc={'benchmarks':{'network':{'operation':'One autoregressive handwriting stroke step: three LSTM dense gates, text attention dense and GMM dense on NPU; recurrent state/attention/window/mixture sampling on CPU. Text and persistent state are initialized/retained internally.',
      'input_shape':[1,3],'output_shape':[1,3],'samples_per_forward':1,'float_boundary':True,
      'quantization':'Five a16w8 dense methods; int16 activations/int8 weights; float32 method boundaries and CPU state.'},'benchmark':[record]}}
    destination=HERE/'benchmark.yml'
    if destination.exists():
        previous=yaml.safe_load(destination.read_text())['benchmarks']['benchmark']
        doc['benchmarks']['benchmark'].extend(r for r in previous if r['run'].get('uart')!='benchmark_uart.txt')
    destination.write_text(yaml.safe_dump(doc,sort_keys=False),encoding='utf-8')
    print(json.dumps({'full_step':{k:v for k,v in record['measurements']['time'].items() if not k.endswith('_samples')},'weights_bytes':weights,'arena_bytes':arena,
                      'methods':{k:{'npu_ms':v['time']['ms'],'wall_ms':v['time']['forward_ms']} for k,v in methods.items()}},indent=2))

if __name__=='__main__': main()
