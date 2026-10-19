#!/usr/bin/env python3
# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Verify raw board heads, decode source faces, and record measured/estimated results."""
import argparse
import csv
from datetime import datetime, timezone
import importlib.metadata
import json
from pathlib import Path
import re
import statistics
import sys
import numpy as np
import yaml

ROOT = Path(__file__).resolve().parents[3]
MODEL = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'scripts'))
from quantized_model import sha256
from validate_networks import load_network
from decode import decode, iou


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-job', required=True)
    parser.add_argument('--execution-job', required=True)
    args = parser.parse_args()
    raw = (MODEL/'benchmark_uart.txt').read_text(encoding='utf-8')
    runs = [part for part in raw.split('Detection network-only test:')[1:] if 'Test_result: PASS' in part]
    assert runs, 'No complete passing run'
    text = runs[-1]
    fixtures = json.loads((MODEL/'board_test_vectors.json').read_text())
    for key, filename in [('source_sha256','yolo-fastest_192_face_v4.tflite'),
                          ('header_sha256','model_test_data.h'), ('graph_sha256','model_quantized.pt2'),
                          ('safetensors_sha256','model_quantized.safetensors'), ('generator_sha256','generate_board_tests.py')]:
        assert sha256(MODEL/filename) == fixtures[key], filename
    actual = [np.zeros((4,n), dtype=np.int16) for n in (648,2592)]
    seen = set()
    for case, head, offset, values in re.findall(r'Detection output (\d+) (\d+) (\d+):([^\r\n]+)',text):
        case,head,offset = map(int,(case,head,offset))
        key=(case,head,offset); assert key not in seen; seen.add(key)
        values=list(map(int,values.split())); assert len(values)==54
        actual[head][case,offset:offset+54]=values
    assert seen=={(case,h,o) for case in range(4) for h,n in enumerate((648,2592)) for o in range(0,n,54)}, 'Incomplete capture'
    assert all(a.min()>=-128 and a.max()<=127 for a in actual)
    comparisons={}
    for baseline in ('default','reference','pytorch'):
        heads=[]
        for h,a in enumerate(actual):
            ref=np.asarray(fixtures['expected'][f'{baseline}_{h}'],dtype=np.int16)
            delta=np.abs(a-ref)
            detail={'max_error_lsb':int(delta.max()), 'mean_error_lsb':float(delta.mean()),
                    'exact_fraction':float((delta==0).mean()),'cases':[]}
            for case in range(4):
                reported=re.search(rf'Detection compare {case} {h} {baseline}: max=(\d+) sum=(\d+) count=(\d+)',text)
                assert reported and tuple(map(int,reported.groups()))==(int(delta[case].max()),int(delta[case].sum()),a.shape[1])
                detail['cases'].append({'pattern':fixtures['patterns'][case], 'max_error_lsb':int(delta[case].max()),
                                        'mean_error_lsb':float(delta[case].mean())})
                if baseline=='default':
                    assert delta[case].max()<=fixtures['initial_board_default_tolerance_lsb']
            heads.append(detail)
        comparisons[baseline]=heads
    decoded={}
    for baseline in ('board','default','reference','pytorch'):
        arrays=actual if baseline=='board' else [np.asarray(fixtures['expected'][f'{baseline}_{h}'],dtype=np.int8) for h in range(2)]
        decoded[baseline]=decode([a[0].astype(np.int8).reshape(fixtures['output_shapes'][h]) for h,a in enumerate(arrays)])
    assert len(decoded['board'])==len(decoded['default'])==2
    overlaps=[max(iou(b['box'],r['box']) for r in decoded['default']) for b in decoded['board']]
    assert min(overlaps)>=.9
    samples=re.findall(r'Benchmark sample (\d+): NPU=(\d+) cycles \(([\d.]+) ms\), forward=(\d+) ticks \(([\d.]+) ms\)',text)
    assert [int(s[0]) for s in samples]==list(range(100))
    cycles,ticks=[int(s[1]) for s in samples],[int(s[3]) for s in samples]
    cpu,npu,timer=map(int,re.search(r'Benchmark clocks: CPU=(\d+) Hz, AXI/NPU=(\d+) Hz, timer=(\d+) Hz',text).groups())
    model,arena,arena_address,weights,weights_address=re.search(r'Benchmark memory: model=(\d+) B, arena=(\d+) B at (0x[0-9a-f]+), weights=(\d+) B at (0x[0-9a-f]+)',text).groups()
    method_peak,temp_peak,reserved=map(int,re.search(r'Benchmark pools: method peak=(\d+) B, temp peak=(\d+) B, reserved=(\d+) B',text).groups())
    active,invocations,overflow=map(int,re.search(r'Benchmark PMU: last active=(\d+) cycles, invocations=(\d+), overflow=(\d+)',text).groups())
    assert active==cycles[-1] and invocations==1 and overflow==0 and min(cycles)>0
    for s in samples:
        assert abs(float(s[2])-1000*int(s[1])/npu)<=.000001
        assert abs(float(s[4])-1000*int(s[3])/timer)<=.000001
    generation=json.loads((ROOT/'ai_layer/generation.json').read_text())
    assert generation['project']=='projects/object_detection/object_detection.cproject.yml'
    assert generation['delegates']==1 and generation['operators']==[]
    assert generation['pte_sha256']==sha256(ROOT/'ai_layer/model.pte') and int(model)==generation['pte_bytes']
    assert generation['safetensors_sha256']==fixtures['safetensors_sha256']
    firmware=ROOT/'out/object_detection/DevKit-E8/Release/object_detection.axf'
    build=yaml.safe_load(firmware.with_name('object_detection.Release+DevKit-E8.cbuild.yml').read_text())['build']
    assert build['context']=='object_detection.Release+DevKit-E8' and build['compiler']=='AC6'
    assert all(flag in build['misc'][language] for language in ('C','CPP') for flag in ('-O3','-ffast-math'))
    provenance=json.loads((MODEL/'benchmark_vela_provenance.json').read_text())
    assert provenance['board_pte_sha256']==generation['pte_sha256']
    assert provenance['summary_sha256']==sha256(MODEL/'benchmark_vela_summary.csv')
    assert all(v['identical'] and v['pte_sha256']==v['estimate_sha256'] for v in provenance['blocks'].values())
    stamp=datetime.now(timezone.utc).isoformat()
    all_decoded={}
    for c,name in enumerate(fixtures['patterns']):
        all_decoded[name]={}
        for baseline in ('board','default','reference','pytorch'):
            arrays=actual if baseline=='board' else [np.asarray(fixtures['expected'][f'{baseline}_{h}'],dtype=np.int8) for h in range(2)]
            all_decoded[name][baseline]=decode([a[c].astype(np.int8).reshape(fixtures['output_shapes'][h]) for h,a in enumerate(arrays)])
    assert all(len(all_decoded[name]['board'])==len(all_decoded[name]['default']) for name in fixtures['patterns'])
    detail={'recorded_at':stamp,'patterns':fixtures['patterns'],'output_shapes':fixtures['output_shapes'],
            'outputs_checked':12960,'comparisons':comparisons,'decoded_source_faces':decoded,'decoded_all_fixtures':all_decoded,'matched_default_iou':overlaps,
            'board_outputs':[a.tolist() for a in actual], 'fixture_sha256':fixtures['header_sha256'],
            'uart_sha256':sha256(MODEL/'benchmark_uart.txt'),'pte_sha256':generation['pte_sha256'],
            'firmware_elf_sha256':sha256(firmware),'harness_sha256':sha256(MODEL/'network_benchmark.cpp'),
            'entry_sha256':sha256(MODEL.parent/'app_main.cpp'),'project_sha256':sha256(MODEL.parent/'object_detection.cproject.yml'),
            'recorder_sha256':sha256(Path(__file__)),
            'arena_address':arena_address,'weights_address':weights_address,'pool_reserved_bytes':reserved,
            'scope':'Network-only fixture fidelity; no labeled-dataset accuracy, live camera, preprocessing or display timing'}
    (MODEL/'board_validation.json').write_text(json.dumps(detail,indent=2)+'\n')
    estimate=next(csv.DictReader((MODEL/'benchmark_vela_summary.csv').open(newline='')))
    config=load_network(MODEL/'network.yml')['vela'];bandwidth={}
    for key,memory,prefix in [('sram','SRAM0','sram'),('weights','MRAM','off_chip_flash')]:
        total=int(estimate[prefix+'_total_bytes'])
        bandwidth[key]={'memory':memory,'vela_memory_area':prefix,'total_bytes_per_inference':total//int(estimate['batch_size']),
                        'average_bytes_per_second':total/float(estimate['inference_time']),
                        'design_peak_bytes_per_second':float(estimate[prefix+'_bandwidth'])*2**30}
    bench={'platform':{'os':'RTX5','os_version':'5.9.1','cpu':'Cortex-M55',
                       'memory':{'weights':{'type':'MRAM'},'activations':{'type':'SRAM0','clock_mhz':npu/1e6}}},
           'measurements':{'memory':{'model_bytes':int(model),'input_tensor_bytes':36864,
                            'ethos':{'weights_region_bytes':int(weights),'arena_region_bytes':int(arena)},
                            'executorch':{'method_pool_peak_bytes':method_peak,'temp_pool_peak_bytes':temp_peak}},
                           'time':{'ms':1000*statistics.median(cycles)/npu,'cycles':statistics.median(cycles),'statistic':'median',
                                   'min_cycles':min(cycles),'max_cycles':max(cycles),'mean_cycles':statistics.mean(cycles),
                                   'forward_ms':1000*statistics.median(ticks)/timer,'forward_timer_hz':timer,
                                   'cycles_samples':cycles,'forward_ticks_samples':ticks}},
           'estimates':{'vela':{**config,'version':importlib.metadata.version('ethos-u-vela'),
                        'accelerator_config':estimate['accelerator_configuration'],'model_sha256':generation['pte_sha256'],
                        'ini_sha256':sha256(MODEL.parent/config['ini']),'summary':'benchmark_vela_summary.csv',
                        'cycles':int(estimate['cycles_total']),'time_ms':1000*float(estimate['inference_time']),'bandwidth':bandwidth}},
           'run':{'recorded_at':stamp,'context':build['context'],'target':'DevKit-E8@Detection_Release',
                  'device':'AE822FA0E5597LS0','core':'M55_HP','npu':'Ethos-U85','macs':256,
                  'packs':{'driver':'ARM::CMSIS-Ethos-U@1.0.0','driver_component':'ARM::Machine Learning:NPU Support:Ethos-U Driver&Generic U85@2.0.0',
                           'board':'AlifSemiconductor::Ensemble@2.2.1','runtime':'PyTorch::ExecuTorch@1.4.1'},
                  'compiler':'AC6@6.24.0','optimize':'-O3 -ffast-math','build_job':args.build_job,'execution_job':args.execution_job,
                  'cpu_clock_mhz':cpu/1e6,'npu_clock_mhz':npu/1e6,'warmup':1,'samples':100,
                  'pmu_cross_check':'PMCCNTR equals PMEVCNTR0 (NPU_ACTIVE) on every inference','pmu_overflow':0,
                  'npu_invocations_per_forward':1,'test_result':'PASS',
                  'max_absolute_error':max(d['max_error_lsb'] for d in comparisons['default']),
                  'tolerance':fixtures['initial_board_default_tolerance_lsb'],'uart':'benchmark_uart.txt',
                  'model_sha256':generation['pte_sha256'],'firmware_sha256':sha256(firmware),
                  'input':'Four fixtures: original source RGB sample after host preprocessing, constant -128, horizontal ramp, random_0 seed 20261019; benchmark repeats random_0; both INT8 heads checked',
                  'timing_scope':'module.forward() including ExecuTorch and PMU hooks; excludes input/output copies, setup, validation, decoding and UART. Four validation forwards then one immediate warmup; no debugger halt during measured loop.',
                  'uart_run':'last complete startup in benchmark_uart.txt','measurement_source':'UART4 COM5 115200 8N1; NPU PMU, RTOS system timer, allocator high-water marks'}}
    network={'operation':'YOLO Fastest 192 face v4; two independently quantized NHWC detection heads, three anchors per cell, six values per anchor',
             'input_shape':[1,192,192,1],'output_shapes':fixtures['output_shapes'],'float_boundary':False,
             'quantization':'Original int8 TFLite quantization retained. Reported tolerance/error is board versus default TFLite in LSB. BUILTIN_REF diagnostic separately in board_validation.json.'}
    (MODEL/'benchmark.yml').write_text(yaml.safe_dump({'benchmarks':{'network':network,'benchmark':[bench]}},sort_keys=False))
    print(json.dumps({'comparisons':comparisons,'decoded_source_faces':decoded,'time':bench['measurements']['time'],
                      'memory':bench['measurements']['memory']},indent=2))


if __name__=='__main__':
    main()
