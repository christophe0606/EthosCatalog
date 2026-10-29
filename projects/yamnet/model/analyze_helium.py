# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Rebuild Helium validation/provenance from retained CMSIS evidence; no board I/O."""
import hashlib
import json
from pathlib import Path
import re
import statistics
import numpy as np
import yaml
from analyze_board_validation import comparison

HERE = Path(__file__).resolve().parent


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    raw = json.loads((HERE / 'helium_scores_memory.json').read_text())
    values = b''.join(bytes.fromhex(c['hex']) for c in raw['chunks'])
    scores = np.frombuffer(values, dtype='<f4').reshape(40, 521)
    original = np.load(HERE / 'evaluation_reference.npz')['scores']
    quantized = np.load(HERE / 'evaluation_quantized.npz')['scores']
    baseline = np.load(HERE / 'board_validation_scores.npz')['scores']
    assert np.isfinite(scores).all() and scores.min() >= 0 and scores.max() <= 1
    assert np.array_equal(scores, baseline), 'Optimization changed retained board scores'
    uart_path = HERE / 'helium_release_uart.txt'
    uart = uart_path.read_text()
    assert 'Test_result: PASS (classification acceptance; score parity reported separately)' in uart
    assert 'Strict score parity (0.005): FAIL (-405)' in uart
    assert 'calls=14 vector=14 fallback=0' in uart
    samples = re.findall(r'Benchmark sample (\d+): NPU=(\d+) cycles [^\n]+forward=(\d+) ticks', uart)
    assert [int(s[0]) for s in samples] == list(range(1, 11))
    cycles, ticks = [[int(s[i]) for s in samples] for i in (1, 2)]
    live = [dict(re.findall(r'(\w+)=([^ ]+)', line)) for line in uart.splitlines()
            if line.startswith('YAMNet live:')]
    assert live and int(live[-1]['infer']) >= 20
    for line in live:
        for field in ('skipped', 'deadline', 'capture_drops', 'overflow', 'requests_error', 'pipeline'):
            assert int(line[field]) == 0, (field, line)
        assert line['infer'] == line['lcd']
    old = yaml.safe_load((HERE / 'benchmark_portable_cpu.yml').read_text())['benchmarks']['benchmark'][0]
    forward_ms = statistics.median(ticks) / 400000
    npu_ms = statistics.median(cycles) / 400000
    report = {
        'scope': '40 synthetic fixtures, 38 independent plus two shared controls; no dataset accuracy claim',
        'vs_portable_cpu': {'scores': 20840, 'bitwise_identical': True,
                            'maximum_absolute_error': float(np.abs(scores - baseline).max())},
        'vs_original_tensorflow': comparison(scores, original),
        'vs_pre_vela_ptq': comparison(scores, quantized),
        'forward_ms': forward_ms, 'npu_ms': npu_ms,
        'cpu_and_other_ms': forward_ms - npu_ms,
        'portable_cpu_forward_ms': old['measurements']['time']['forward_ms'],
        'complete_forward_speedup': old['measurements']['time']['forward_ms'] / forward_ms,
        'cpu_convolution_last_ms': float(re.search(r'fallback=0 time_ms=([\d.]+)', uart).group(1)),
        'live_last': live[-1], 'npu_cycles': cycles, 'forward_ticks': ticks,
        'raw_float_bytes_sha256': hashlib.sha256(values).hexdigest(),
        'raw_memory_sha256': sha(HERE / 'helium_scores_memory.json'),
    }
    (HERE / 'helium_metrics.json').write_text(json.dumps(report, indent=2) + '\n')
    # Capture provenance is immutable. Do not rebind historical measurements to
    # whichever ELF or source files happen to be present when analysis is rerun.
    provenance = json.loads((HERE / 'helium_release_provenance.json').read_text())
    assert provenance['uart_sha256'] == sha(uart_path)
    assert provenance['cmsis_snapshot_sha256'] == sha(HERE / 'helium_release_cmsis.json')
    assert provenance['scores_sha256'] == sha(HERE / 'helium_board_scores.npz')
    assert np.array_equal(np.load(HERE / 'helium_board_scores.npz')['scores'], scores)
    assert provenance['model_sha256'] == 'bd5c2b892839f8d42467f284a448cbc35a2510a4930959ae1dd5e4987deb86b4'
    print(json.dumps({k: report[k] for k in ('forward_ms', 'npu_ms', 'cpu_and_other_ms',
          'complete_forward_speedup', 'vs_portable_cpu', 'live_last')}, indent=2))


if __name__ == '__main__':
    main()
