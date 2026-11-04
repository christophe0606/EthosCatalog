#!/usr/bin/env python3
# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Compare imported PyTorch Q/DQ and source TFLite, including recurrent feedback."""
import json, sys
from pathlib import Path
import numpy as np
import torch
import tensorflow as tf
import yaml
from model import RNNoise, SOURCE, INPUT_SHAPES
sys.path.insert(0,str(Path(__file__).parent/'upstream'))
from rnnoise_pre_processing import RNNoisePreProcess

P=Path(__file__).parent

def quantize(x, params):
    return np.clip(np.rint(np.asarray(x)/params[0])+params[1],-128,127).astype(np.int8)

def main():
    torch.set_num_threads(1)
    model=RNNoise().eval()
    reference=tf.lite.Interpreter(model_path=str(SOURCE),experimental_op_resolver_type=tf.lite.experimental.OpResolverType.BUILTIN_REF)
    reference.allocate_tensors()
    def run(inputs):
        for i,x in zip(model.inputs,inputs): reference.set_tensor(i,np.asarray(x,dtype=np.int8))
        reference.invoke()
        return [reference.get_tensor(i) for i in model.outputs]
    source=[np.load(P/'upstream'/('testing_input_'+name+'_0.npy')) for name in ('main_input_int8','vad_gru_prev_state_int8','noise_gru_prev_state_int8','denoise_gru_prev_state_int8')]
    expected=[np.load(P/'upstream'/('testing_output_'+name+'_0.npy')) for name in ('Identity_int8','Identity_1_int8','Identity_2_int8','Identity_3_int8','Identity_4_int8')]
    ref=run(source)
    out=[x.numpy() for x in model(*(torch.from_numpy(x.copy()) for x in source))]
    report={'source_fixture_max_lsb_vs_upstream':[int(np.max(abs(x.astype(int)-y.astype(int)))) for x,y in zip(ref,expected)],'source_fixture_pytorch_vs_tflite_max_lsb':[int(np.max(abs(x.astype(int)-y.astype(int)))) for x,y in zip(out,ref)]}
    rng=np.random.default_rng(87)
    pcm=[];features=[];silence=[];dsp=RNNoisePreProcess(training=False)
    for n in range(160):
        t=(np.arange(480)+480*n)/48000
        block=np.zeros(480) if n<16 or n>=128 else (6000*(1+np.sin(2*np.pi*3*t))*.5*(np.sin(2*np.pi*173*t)+.3*np.sin(2*np.pi*346*t))+rng.normal(0,300,480))
        block=np.clip(np.rint(block),-32768,32767).astype(np.int16)
        # The upstream process_frame unpacks seven results, but its silence
        # branch returns only two. Consume the documented feature pair directly
        # without editing the preserved upstream file.
        filtered,dsp.mem_hp_x=dsp._biquad(block.astype(float),b_hp=(-2,1),a_hp=(-1.99599,.996),mem_hp_x=dsp.mem_hp_x)
        result=dsp._compute_frame_features(filtered);pcm.append(block);silence.append(result[0]);features.append(result[1])
    features=np.array(features,dtype=np.float32)
    np.savez(P/'frontend_reference.npz',pcm=np.array(pcm),features=features,silence=np.array(silence))
    reset=lambda:[np.full(shape,model.params[i][1],np.int8) for shape,i in zip(INPUT_SHAPES[1:],model.inputs[1:])]
    rs=reset();ps=reset();maxerr=np.zeros(5,dtype=int);total=np.zeros(5);counts=np.zeros(5);traces=[];source_traces=[];shared_max=np.zeros(5,dtype=int);shared_total=np.zeros(5)
    for feature in features:
        x=quantize(feature.reshape(1,1,42),model.params[0])
        ri=run([x]+rs);po=[v.numpy() for v in model(*(torch.from_numpy(v.copy()) for v in [x]+ps))]
        shared=[v.numpy() for v in model(*(torch.from_numpy(v.copy()) for v in [x]+rs))]
        for k,(a,b) in enumerate(zip(ri,po)):
            e=np.abs(a.astype(int)-b.astype(int));maxerr[k]=max(maxerr[k],e.max());total[k]+=e.sum();counts[k]+=e.size
            se=np.abs(a.astype(int)-shared[k].astype(int));shared_max[k]=max(shared_max[k],se.max());shared_total[k]+=se.sum()
        rs=[ri[3].reshape(1,24),ri[2].reshape(1,48),ri[0].reshape(1,96)]
        ps=[po[3].reshape(1,24),po[2].reshape(1,48),po[0].reshape(1,96)]
        traces.append(np.concatenate([v.reshape(-1) for v in po]));source_traces.append(np.concatenate([v.reshape(-1) for v in ri]))
    np.savez(P/'recurrent_reference.npz',features=features,torch_outputs=np.array(traces),tflite_outputs=np.array(source_traces))
    report.update(sequence_frames=len(features),sequence_description='160 continuous 10ms frames: 16 silence, 112 amplitude-modulated 173/346Hz harmonic tone plus seeded noise, 32 silence; independently fed-back states. Not a speech quality test.',sequence_max_lsb=maxerr.tolist(),sequence_mean_lsb=(total/counts).tolist(),output_order=['denoise_state','gains','noise_state','vad_state','voice_probability'])
    scales=np.array([model.params[i][0] for i in model.outputs])
    report.update(sequence_max_absolute_error=(maxerr*scales).tolist(),sequence_mean_absolute_error=(total/counts*scales).tolist(),sequence_max_fraction_of_tensor_range=(maxerr/255).tolist(),shared_state_one_step_max_lsb=shared_max.tolist(),shared_state_one_step_mean_lsb=(shared_total/counts).tolist())
    # Reconstructed float means quantization removed from the recovered INT8 weights;
    # it is not the unavailable pre-quantization training checkpoint.
    floatmodel=RNNoise(quantized=False).eval()
    fp=[torch.zeros(shape) for shape in INPUT_SHAPES[1:]];floaterr=[]
    for feature,trace in zip(features,traces):
        fo=floatmodel(torch.from_numpy(feature.reshape(1,1,42)),*fp)
        fp=[fo[3].reshape(1,24),fo[2].reshape(1,48),fo[0].reshape(1,96)]
        qgain=(trace[96:118].astype(float)+128)/256
        floaterr.append(np.abs(fo[1].detach().numpy().reshape(-1)-qgain))
    report['recovered_weight_float_vs_int8_gain_mean_absolute_error']=float(np.mean(floaterr))
    report['recovered_weight_float_vs_int8_gain_max_absolute_error']=float(np.max(floaterr))
    report['original_float_quantization_error']='Not published by upstream; original FP32 checkpoint unavailable. PESQ 2.945 is an upstream end-task score, not quantization error.'
    (P/'conversion_metrics.json').write_text(json.dumps(report,indent=2)+'\n')
    document=yaml.safe_load((P/'network.yml').read_text());document['network']['quantization']['error']=report
    (P/'network.yml').write_text('# Copyright 2026 Arm Limited and/or its affiliates.\n# SPDX-License-Identifier: Apache-2.0\n'+yaml.safe_dump(document,sort_keys=False))
    lines=['// Copyright 2026 Arm Limited and/or its affiliates.','// SPDX-License-Identifier: Apache-2.0','#pragma once','#include <stdint.h>']
    def array(name,x):
        flat=np.asarray(x).reshape(-1)
        lines.append('static const int8_t '+name+'['+str(len(flat))+'] = {'+', '.join(str(int(v)) for v in flat)+'};')
    for i,x in enumerate(source):array('rnnoise_test_input_'+str(i),x)
    for i,x in enumerate(out):array('rnnoise_test_output_'+str(i),x)
    for i,x in enumerate(ref):array('rnnoise_source_test_output_'+str(i),x)
    for label,index in [('FEATURE',0),('VAD_STATE',1),('NOISE_STATE',2),('DENOISE_STATE',3),('GAIN',104),('PROBABILITY',108)]:
        lines.append('#define RNNOISE_'+label+'_SCALE '+format(model.params[index][0],'.17g')+'f')
        lines.append('#define RNNOISE_'+label+'_ZERO_POINT '+str(model.params[index][1]))
    lines.extend(['#define RNNOISE_TEST_OUTPUT_TOLERANCE_LSB 8',''])
    (P/'model_test_data.h').write_text('\n'.join(lines))
    sequence=['// Copyright 2026 Arm Limited and/or its affiliates.','// SPDX-License-Identifier: Apache-2.0','#pragma once','#include <stdint.h>','#define RNNOISE_SEQUENCE_FRAMES 160','#define RNNOISE_SEQUENCE_OUTPUT_SIZE 191']
    for name,values in [('rnnoise_sequence_features',[quantize(f,model.params[0]).reshape(-1) for f in features]),('rnnoise_sequence_host_outputs',traces),('rnnoise_sequence_source_outputs',source_traces)]:
        sequence.append('static const int8_t '+name+'[160]['+str(len(values[0]))+'] = {')
        sequence.extend('{'+', '.join(str(int(v)) for v in row)+'},' for row in values)
        sequence.append('};')
    (P/'sequence_test_data.h').write_text('\n'.join(sequence)+'\n')
    print(json.dumps(report,indent=2),flush=True)
    assert max(report['source_fixture_max_lsb_vs_upstream'])==0,report
    # Explicit demo acceptance limits, not a speech-quality claim. Fixed-point
    # requantization differs from ideal Q/DQ and amplifies through recurrent GRUs.
    assert report['sequence_mean_absolute_error'][1]<.03 and report['sequence_max_absolute_error'][1]<.16,report
    assert report['sequence_mean_absolute_error'][4]<.01 and report['sequence_max_absolute_error'][4]<.10,report

if __name__=='__main__':main()
