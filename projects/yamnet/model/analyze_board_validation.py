# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Analyze raw CMSIS debugger snapshots; never connects to the board itself."""
import hashlib
import json
from pathlib import Path
import numpy as np

HERE=Path(__file__).resolve().parent


def comparison(actual,reference):
    error=np.abs(actual-reference)
    top5a=np.argsort(actual,axis=1)[:,-5:]
    top5r=np.argsort(reference,axis=1)[:,-5:]
    independent=[1]+list(range(3,len(actual)))
    return {'mean_absolute_score_error':float(error.mean()),
            'max_absolute_score_error':float(error.max()),
            'top1_agreement':float(np.mean(actual.argmax(1)==reference.argmax(1))),
            'top1_agreement_independent':float(np.mean(actual[independent].argmax(1)==reference[independent].argmax(1))),
            'mean_top5_set_overlap':float(np.mean([len(set(a)&set(r))/5 for a,r in zip(top5a,top5r)])),
            'reference_top1_in_board_top5':float(np.mean([a in b for a,b in zip(reference.argmax(1),top5a)])),
            'per_pattern_max_abs_error':error.max(1).tolist(),
            'per_pattern_mean_abs_error':error.mean(1).tolist()}


def main():
    raw=np.load(HERE/'board_validation_scores.npz')
    actual=raw['scores'];assert actual.shape==(40,521) and np.isfinite(actual).all()
    reference=np.load(HERE/'evaluation_reference.npz')['scores']
    quantized=np.load(HERE/'evaluation_quantized.npz')['scores']
    report={'scope':'40 synthetic patterns:38 independent plus shared silence/440Hz controls; no labelled dataset accuracy',
            'vs_original_tensorflow':comparison(actual,reference),
            'vs_pre_vela_ptq':comparison(actual,quantized),
            'board_top1':actual.argmax(1).tolist(),
            'original_top1':reference.argmax(1).tolist(),
            'pre_vela_top1':quantized.argmax(1).tolist(),
            'npu_cycles':raw['npu_cycles'].tolist(),'forward_ticks':raw['forward_ticks'].tolist(),
            'benchmark_npu_cycles':raw['benchmark_npu_cycles'].tolist(),
            'benchmark_forward_ticks':raw['benchmark_forward_ticks'].tolist(),
            'raw_sha256':hashlib.sha256((HERE/'board_validation_scores.npz').read_bytes()).hexdigest()}
    (HERE/'board_validation_metrics.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:report[k] for k in ['vs_original_tensorflow','vs_pre_vela_ptq']},indent=2))


if __name__=='__main__':main()
