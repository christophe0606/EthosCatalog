# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Verify persisted float/quantized artifacts against original Keras fixtures."""
from pathlib import Path
import hashlib,json,sys
import numpy as np
import torch
from model import YAMNet
ROOT=Path(__file__).resolve().parents[3]
HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT/'scripts'))
from project_config import selected_project
from quantized_model import load_quantized_program


def main():
    torch.set_num_threads(1)
    manifest=json.loads((HERE/'upstream/provenance.json').read_text())
    for name,asset in manifest['assets'].items():
        assert hashlib.sha256((HERE/'upstream'/name).read_bytes()).hexdigest()==asset['sha256'],name
    _,_,project,_,network=selected_project(project='yamnet')
    q=load_quantized_program('ethos-u85-256',project.parent,network).module(check_guards=False)
    f=YAMNet().eval()
    patterns=np.load(HERE/'evaluation_reference.npz'); expected=np.load(HERE/'evaluation_quantized.npz')['scores']
    float_error=0.;cache_error=0.
    with torch.no_grad():
        for i,x in enumerate(patterns['inputs']):
            tx=torch.from_numpy(x[None].copy())
            float_error=max(float_error,float(np.abs(f(tx).numpy()[0]-patterns['scores'][i]).max()))
            cache_error=max(cache_error,float(np.abs(q(tx).numpy()[0]-expected[i]).max()))
    assert float_error<1e-4 and cache_error==0,(float_error,cache_error)
    report=json.loads((HERE/'conversion_metrics.json').read_text())
    assert report['activation']=='int16' and report['top1_agreement']>=.9
    assert report['max_absolute_score_error']<.11 and report['mean_absolute_score_error']<.0004
    print(f'PASS: upstream hashes, float Keras fixtures max error={float_error}, saved PT2 quantized fixture error={cache_error}')

if __name__=='__main__':main()
