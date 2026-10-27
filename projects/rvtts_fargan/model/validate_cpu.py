#!/usr/bin/env python3
# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Closed-loop CPU FARGAN check against independent original float synthesis.

Acceptance gates, declared before board measurements: >=30 dB PCM SNR,
<=0.001 normalized PCM RMSE and <=0.01 peak normalized PCM difference.
These numerical gates do not replace the user's speaker listening check.
"""
import ctypes
import json
from pathlib import Path
import shutil
import subprocess
import numpy as np
from validate import build_reference

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parent


def c_array(name, array):
    a=np.asarray(array,np.float32).reshape(-1)
    return f"static const float {name}[{len(a)}]={{\n"+",\n".join(",".join(f"{float(v):.9e}f" for v in a[i:i+8]) for i in range(0,len(a),8))+"\n};\n"


def main():
    reference=build_reference()
    build=PROJECT.parents[1]/"tmp/rvtts-reference-fargan"
    compiler=shutil.which("clang")
    subprocess.run([compiler,"-shared","-O3","-ffast-math","-DRVTTS_FARGAN_HOST",
                    "-D_CRT_SECURE_NO_WARNINGS",str(PROJECT/"nodes_impl/fargan_cpu.c"),
                    "-o",str(build/"cpu.dll")],check=True)
    cpu=ctypes.CDLL(str(build/"cpu.dll"))
    cpu.rvtts_fargan_cpu_bind.argtypes=[ctypes.c_void_p,ctypes.c_size_t]
    cpu.rvtts_fargan_cpu_prime.argtypes=[ctypes.c_void_p,ctypes.c_void_p]
    cpu.rvtts_fargan_cpu_frame.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_void_p]
    blob=(HERE/"fargan_cpu_weights.bin").read_bytes()
    storage=ctypes.create_string_buffer(blob)
    assert cpu.rvtts_fargan_cpu_bind(storage,len(blob))==0
    source=np.load(HERE/"source_fixtures.npz")
    features=np.ascontiguousarray(source["features"],dtype=np.float32)
    context=(ctypes.c_uint64*1024)()
    reference.rv_prime(features[0].ctypes.data)
    cpu.rvtts_fargan_cpu_prime(context,features[0].ctypes.data)
    expected,actual=[],[]
    for frame in features:
        a,b=np.zeros(160,np.float32),np.zeros(160,np.float32)
        reference.rv_run(frame.ctypes.data,a.ctypes.data)
        cpu.rvtts_fargan_cpu_frame(context,frame.ctypes.data,b.ctypes.data)
        expected.append(a);actual.append(b)
    expected,actual=np.array(expected),np.array(actual)
    assert np.isfinite(actual).all()
    difference=actual-expected
    rms=float(np.sqrt(np.mean(expected.astype(np.float64)**2)))
    rmse=float(np.sqrt(np.mean(difference.astype(np.float64)**2)))
    maximum=float(np.max(np.abs(difference)))
    snr=float(20*np.log10(rms/max(rmse,1e-30)))
    result=dict(reference="Original rvTTS FARGAN scalar float matrix synthesis with original LUT activations",
                implementation="Per-output source float weight requantization to int16; dynamic int16 inputs; exact int64 dot sums; float32 nonlinearities/state",
                frames=len(features),samples=actual.size,max_absolute_error=maximum,rmse=rmse,reference_rms=rms,snr_db=snr,
                limits=dict(min_snr_db=30,max_rmse=.001,max_absolute_error=.01),
                result="PASS" if snr>=30 and rmse<=.001 and maximum<=.01 else "FAIL")
    (HERE/"cpu_equivalence.json").write_text(json.dumps(result,indent=2)+"\n")
    np.savez(HERE/"cpu_fixtures.npz",features=features,source_pcm=expected,cpu_pcm=actual)
    header="// Copyright 2026 Arm Limited and/or its affiliates.\n// SPDX-License-Identifier: Apache-2.0\n#pragma once\n"
    header+=f"#define RVTTS_CPU_FIXTURE_FRAMES {len(features)}u\n"
    header+=c_array("rvtts_cpu_fixture_features",features)
    header+=c_array("rvtts_cpu_fixture_source_pcm",expected)
    header+=c_array("rvtts_cpu_fixture_quantized_pcm",actual)
    (HERE/"cpu_test_data.h").write_text(header)
    print(json.dumps(result,indent=2),flush=True)
    assert result["result"]=="PASS"


if __name__=="__main__":main()
