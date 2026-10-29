#!/usr/bin/env python3
# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Summarize the retained CMSIS Release UART capture; never accesses hardware."""
import hashlib
import json
from pathlib import Path
import re
import statistics
import yaml

HERE = Path(__file__).resolve().parent


def main():
    uart = (HERE / 'benchmark_release_uart.txt').read_text()
    provenance = json.loads((HERE / 'benchmark_release_provenance.json').read_text())
    assert hashlib.sha256((HERE / 'benchmark_release_uart.txt').read_bytes()).hexdigest() == provenance['uart_sha256']
    assert 'Test_result: PASS (classification acceptance; score parity reported separately)' in uart
    assert 'Strict score parity (0.005): FAIL (-405)' in uart
    assert 'Classification: PTQ top1=40/40, TensorFlow top1=39/40, TensorFlow top1 in board top5=40/40' in uart
    clocks = tuple(map(int, re.search(r'CPU=(\d+) Hz, AXI/NPU=(\d+) Hz, timer=(\d+) Hz', uart).groups()))
    assert clocks == (400000000, 400000000, 400000000)
    samples = re.findall(r'Benchmark sample (\d+): NPU=(\d+) cycles \([^\n]+forward=(\d+) ticks', uart)
    assert [int(s[0]) for s in samples] == list(range(1, 11))
    cycles = [int(s[1]) for s in samples]
    ticks = [int(s[2]) for s in samples]
    model, method, temp, reserved = map(int, re.search(r'Benchmark memory: model=(\d+) B, method peak=(\d+) B, temp peak=(\d+) B, reserved=(\d+) B', uart).groups())
    calls, active, overflow = map(int, re.search(r'Benchmark PMU: invocations=(\d+), active=(\d+) cycles, overflow=(\d+)', uart).groups())
    assert calls == 14 and overflow == 0 and active == cycles[-1]
    backend, original = map(float, re.search(r'Errors: backend=([\d.]+), original_float=([\d.]+)', uart).groups())
    result = {'benchmarks': {
        'network': {
            'operation': 'YAMNet sound event classification of one 96x64 log-mel patch; CPU float first/depthwise convolutions and fourteen automatically partitioned Ethos delegates for pointwise/head work.',
            'input_shape': [1, 96, 64], 'output_shape': [1, 521],
            'sample_rate_hz': 16000, 'samples_per_forward': 15600, 'float_boundary': True,
            'quantization': 'Float first/depthwise CPU convolutions; symmetric int16 activations and per-channel int8 pointwise/head weights on Ethos-U85.'},
        'benchmark': [{
            'platform': {'os': 'RTX5', 'os_version': '5.9.1', 'cpu': 'Cortex-M55',
                'memory': {'weights': {'type': 'OSPI1; read in place without a full model SRAM copy', 'clock_mhz': 100,
                    'clock_source': 'hardware_params.cpp guarded e8_flash_set_read_clock(100000000); AXI400MHz/divider4 verified in board_validation_provenance.json'},
                    'activations': {'type': 'SRAM0', 'clock_mhz': 400}}},
            'measurements': {'memory': {'model_bytes': model, 'input_tensor_bytes': 24576,
                'executorch': {'method_pool_peak_bytes': method, 'temp_pool_peak_bytes': temp}},
                'time': {'ms': statistics.median(cycles)*1000/clocks[1], 'cycles': statistics.median(cycles),
                    'statistic': 'median', 'min_cycles': min(cycles), 'max_cycles': max(cycles),
                    'mean_cycles': statistics.mean(cycles), 'forward_ms': statistics.median(ticks)*1000/clocks[2],
                    'forward_timer_hz': clocks[2], 'cycles_samples': cycles, 'forward_ticks_samples': ticks}},
            'run': {'recorded_at': provenance['recorded_at'], 'context': 'yamnet.Release+DevKit-E8',
                'target': 'DevKit-E8@YAMNet_Release', 'device': 'AE822FA0E5597LS0', 'core': 'M55_HP',
                'npu': 'Ethos-U85', 'macs': 256,
                'packs': {'driver': 'ARM::CMSIS-Ethos-U@1.0.0', 'driver_component': 'ARM::Machine Learning:NPU Support:Ethos-U Driver&Generic U85@2.0.0', 'board': 'AlifSemiconductor::Ensemble@2.2.1', 'runtime': 'PyTorch::ExecuTorch@1.4.1'},
                'compiler': 'AC6@6.24.0', 'optimize': '-O3 -ffast-math',
                'build_job': provenance['build_job'], 'execution_job': provenance['execution_job'],
                'cpu_clock_mhz': 400, 'npu_clock_mhz': 400, 'warmup': 40, 'samples': 10,
                'pmu_cross_check': 'Every forward: exactly14 completed calls, PMCCNTR sum equals NPU_ACTIVE sum, nonzero cycles, no overflow.',
                'pmu_overflow': overflow, 'npu_invocations_per_forward': calls,
                'test_result': 'PASS', 'max_absolute_error': backend,
                'uart': 'benchmark_release_uart.txt', 'model_sha256': provenance['model_sha256'],
                'firmware_sha256': provenance['firmware_sha256'],
                'input': 'Forty synthetic fixtures (38 independent plus silence/440Hz controls), then ten warmed calls cycling fixtures0,1,2. Classification acceptance: 40/40 PTQ top1; at least95% TensorFlow top1 (observed39/40); TensorFlow top1 in board top5 for40/40; finite scores in[0,1]. No labelled-dataset accuracy claim.',
                'timing_scope': 'ms/cycles are the sum of14 NPU PMU intervals only. forward_ms measures complete module.forward including CPU convolutions/padding/ReLU and Q/DQ; excludes input assembly, output copy, validation, CRC/loading, UART, frontend and LCD. Full-forward latency is the scheduling constraint.',
                'uart_run': 'Autonomous startup with no manual start gate. Matching-ELF debug session continued from main; all50 forwards complete without breakpoints, halts or debug reads during execution.',
                'measurement_source': f'CMSIS serial COM5 1152008N1 and completion debugger snapshot in benchmark_release_provenance.json. Method/temp reservations1048576/524288B; observed peaks{method}/{temp}B. Strict score parity0.005 FAIL(-405), independently of classification PASS: max board/PTQ error{backend}, max board/TensorFlow error{original}. Original exhaustive scores/metrics and strict failure remain in board_validation_*.'}}]}}
    (HERE / 'benchmark.yml').write_text(yaml.safe_dump(result, sort_keys=False, width=100))
    print(json.dumps(result['benchmarks']['benchmark'][0]['measurements'], indent=2))


if __name__ == '__main__':
    main()
