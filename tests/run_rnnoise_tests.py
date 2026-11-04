# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Host DSP regression; run in a compiler developer environment or pass --executable.
Uses only the vendored, Apache-2.0 original Python reference and synthetic fixture.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import numpy as np
ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--compiler', default='cl' if shutil.which('cl') else 'clang++')
p.add_argument('--executable', type=Path)
p.add_argument('--lcd-executable', type=Path)
a = p.parse_args()
out = ROOT/'out/rnnoise-tests';out.mkdir(parents=True, exist_ok=True)
exe = a.executable.resolve() if a.executable else out/'rnnoise_host.exe'
if not a.executable:
    sources = ['tests/rnnoise_host.cpp','common/nodes_impl/c/rnnoise/rnnoise_dsp.cpp','common/nodes_impl/c/rnnoise/RNNoiseFeatureProcessor.cc']
    if Path(a.compiler).stem.lower() == 'cl':
        command = [a.compiler,'/nologo','/std:c++17','/EHsc','/O2','/Icommon/nodes_impl/c/rnnoise',*sources,'/Fe:'+str(exe),'/Fo:'+str(out)+'/']
    else:
        command = [a.compiler,'-std=c++17','-O2','-Icommon/nodes_impl/c/rnnoise',*sources,'-o',str(exe)]
    subprocess.run(command,cwd=ROOT,check=True)
fixture=np.load(ROOT/'projects/rnnoise/model/frontend_reference.npz')
pcm=fixture['pcm'].astype(np.int16)
def run(name, frames, gain, reset=999999):
    source=out/(name+'.pcm');features=out/(name+'.features');audio=out/(name+'.audio')
    frames.tofile(source)
    subprocess.run([str(exe),str(source),str(features),str(audio),str(gain),str(reset)],check=True)
    return np.fromfile(features,dtype=np.float32).reshape(-1,42), np.fromfile(audio,dtype=np.float32).reshape(-1,480)
f,y=run('full',pcm,.25)
error=np.abs(f-fixture['features'])
q=lambda x: np.clip(np.rint(x/np.float32(.22150060534477234))+14,-128,127).astype(np.int16)
qerror=np.abs(q(f)-q(fixture['features']))
assert error.max()<.03, error.max()
assert qerror.max()<=1, qerror.max()
spec=importlib.util.spec_from_file_location('rnnoise_reference',ROOT/'projects/rnnoise/model/upstream/rnnoise_pre_processing.py')
module=importlib.util.module_from_spec(spec)
# The upstream silence branch returns two values although process_frame expects
# seven. Repair only that tuple in memory so its unchanged FFT/synthesis can run.
source=spec.origin
code=Path(source).read_text().replace('return True, features', 'return True, features, X, P, Ex, Ep, Exp')
exec(compile(code,source,'exec'),module.__dict__)
ref=module.RNNoisePreProcess(False)
expected=[]
for frame in pcm:
    values=ref.process_frame(frame.astype(np.float64))
    expected.append(ref.post_process(values[0],np.full(22,.25),*values[2:])/32768.)
expected=np.array(expected)
audio_error=np.abs(y-expected)
assert audio_error.max()<1.e-4, audio_error.max()
_,unity=run('unity',pcm,1)
_,zero=run('zero',pcm,0)
assert np.linalg.norm(y)<np.linalg.norm(unity)*.4
assert np.max(np.abs(zero))<1.e-4  # silence deliberately bypasses the mask
reset_features,reset_audio=run('reset',pcm,.25,80)
tail_features,tail_audio=run('tail',pcm[80:],.25)
assert np.array_equal(reset_features[80:],tail_features)
assert np.array_equal(reset_audio[80:],tail_audio)
zeros=np.zeros((4,480),dtype=np.int16)
zf,zy=run('silence',zeros,1)
assert not zf.any() and not zy.any()
metrics={'frames':len(pcm),'feature_max_abs':float(error.max()),'feature_mean_abs':float(error.mean()),'quantized_feature_max_lsb':int(qerror.max()),'quantized_feature_different_values':int(np.count_nonzero(qerror)),'feature_values':int(qerror.size),'synthesis_max_normalized_abs':float(audio_error.max()),'synthesis_mean_normalized_abs':float(audio_error.mean()),'reset_bit_exact':True,'silence_zero':True,'zero_mask_max_normalized_abs':float(np.abs(zero).max())}
(out/'metrics.json').write_text(json.dumps(metrics,indent=2)+'\n')
print(json.dumps(metrics,indent=2))

# Render the production landscape UI and check framebuffer sentinels, including
# maximum counter/timer values, to catch text-layout writes beyond the buffer.
lcd_exe=a.lcd_executable.resolve() if a.lcd_executable else out/'rnnoise_lcd_host.exe'
if not a.lcd_executable:
    sources=['tests/rnnoise_lcd_host.cpp','projects/rnnoise/nodes_impl/rnnoise_ui.c']
    if Path(a.compiler).stem.lower()=='cl':
        command=[a.compiler,'/nologo','/EHsc','/O2',*sources,'/Fe:'+str(lcd_exe),'/Fo:'+str(out)+'/']
    else:
        command=[a.compiler,'-std=c++17','-O2',sources[0],'-x','c',sources[1],'-o',str(lcd_exe)]
    subprocess.run(command,cwd=ROOT,check=True)
subprocess.run([str(lcd_exe),str(out/'lcd-preview.ppm')],check=True)
print('Landscape LCD rendering and framebuffer bounds: PASS')
