#!/usr/bin/env python3
# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Independent FARGAN C reference using original function bodies and LUTs.

Only host test glue, float matrix storage and a scalar matrix-vector kernel are
provided here. Neural recurrence and synthesis functions come from the original
BSD source; their function bodies are copied without edits when building.
"""
import ctypes
import json
from pathlib import Path
import re
import shutil
import subprocess
import numpy as np
import torch
from safetensors.torch import save_file
from model import HERE, Fargan, FarganInit, load_arrays, frame_input, pitch_indices


def function(text, name):
    match = re.search(r"(?:static )?void " + name + r"\(", text)
    if not match:
        raise ValueError(name)
    start = text.index("{", match.start())
    depth, end = 1, start+1
    while depth:
        if text[end] == "{": depth += 1
        if text[end] == "}": depth -= 1
        end += 1
    return text[match.start():end]


def build_reference():
    upstream = HERE / "upstream"
    build = HERE.parents[2] / "tmp/rvtts-reference-fargan"
    build.mkdir(parents=True, exist_ok=True)
    arrays = load_arrays()
    data = (upstream / "fargan_data.c").read_text()
    layers = re.findall(r'linear_init\(&model->(\w+), arrays, (.*?), (\d+), (\d+)\)', data)
    assignments, buffers, offset = [], [], 0
    for name, arguments, inputs, outputs in layers:
        inputs, outputs = int(inputs), int(outputs)
        weight = arrays[name+"_weights_float"]
        bias = arrays.get(name+"_bias")
        assignments.append(f"st.model.{name}.nb_inputs={inputs}; st.model.{name}.nb_outputs={outputs}; st.model.{name}.float_weights=weights+{offset};")
        buffers.append(weight); offset += len(weight)
        if bias is not None:
            assignments.append(f"st.model.{name}.bias=weights+{offset};")
            buffers.append(bias); offset += len(bias)
    np.concatenate(buffers).astype("<f4").tofile(build / "reference_weights.bin")
    source = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define OPUS_INLINE inline
#define OPUS_COPY(d,s,n) memcpy(d,s,(n)*sizeof(*(d)))
#define OPUS_MOVE(d,s,n) memmove(d,s,(n)*sizeof(*(d)))
#define OPUS_CLEAR(d,n) memset(d,0,(n)*sizeof(*(d)))
#define IMAX(a,b) ((a)>(b)?(a):(b))
#define IMIN(a,b) ((a)<(b)?(a):(b))
#define MIN32 IMIN
#define MAX32 IMAX
#define MIN16 IMIN
#define celt_assert assert
#define NB_FEATURES 20
#define NB_BANDS 18
#define PITCH_MAX_PERIOD 256
#define FARGAN_FEATURES 20
#define MAX_RNN_NEURONS_ALL 160
#define MAX_INPUTS 2048
#define MAX_CONV_INPUTS_ALL 1024
#define ACTIVATION_LINEAR 0
#define ACTIVATION_SIGMOID 1
#define ACTIVATION_TANH 2
typedef short opus_int16;
typedef struct {const float *bias; const float *float_weights; int nb_inputs,nb_outputs;} LinearLayer;
typedef struct {int unused;} WeightArray;
#include "upstream/activation_lut.h"
'''
    # Preserve original license notices in this generated host adapter.
    for filename in ("nnet.c", "fargan.c"):
        original = (upstream / filename).read_text()
        source = original[:original.index("#")] + "\n" + source
    for filename in ("fargan_data.h", "fargan.h"):
        original = (upstream / filename).read_text()
        source += re.sub(r'^#include.*$', '', original, flags=re.M) + "\n"
    source += r'''
static void compute_linear(const LinearLayer *l,float *out,const float *in,int arch) {
 for(int o=0;o<l->nb_outputs;o++) {float sum=0; for(int i=0;i<l->nb_inputs;i++)sum+=l->float_weights[i*l->nb_outputs+o]*in[i]; out[o]=sum+(l->bias?l->bias[o]:0);}
}
static void compute_activation(float *out,const float *in,int n,int activation,int arch) {
 if(activation==ACTIVATION_TANH)vec_tanh_lut(out,in,n);
 else if(activation==ACTIVATION_SIGMOID)vec_sigmoid_lut(out,in,n);
 else if(out!=in)memcpy(out,in,n*sizeof(float));
}
'''
    nnet = (upstream / "nnet.c").read_text()
    fargan = (upstream / "fargan.c").read_text()
    for name in ("compute_generic_dense", "compute_generic_gru", "compute_glu", "compute_generic_conv1d"):
        source += function(nnet, name) + "\n"
    for name in ("compute_fargan_cond", "fargan_deemphasis", "run_fargan_subframe", "fargan_cont", "fargan_synthesize_impl"):
        source += function(fargan, name) + "\n"
    source += 'static FARGANState st; static float *weights;\n'
    source += '__declspec(dllexport) int rv_load(const char *path) {FILE *f=fopen(path,"rb"); if(!f)return -1; weights=malloc('+str(offset)+'*4); fread(weights,4,'+str(offset)+',f); fclose(f); memset(&st,0,sizeof(st));\n'
    source += "\n".join(assignments) + '\nreturn 0;}\n'
    source += r'''
__declspec(dllexport) void rv_prime(const float *features) {float zeros[320]={0}, f[100]; for(int i=0;i<5;i++)memcpy(f+i*20,features,80); fargan_cont(&st,zeros,f);}
__declspec(dllexport) void rv_run(const float *features,float *pcm) {fargan_synthesize_impl(&st,pcm,features);}
__declspec(dllexport) void rv_state(float *out) {
 memcpy(out,st.pitch_buf,256*4); memcpy(out+256,st.cond_conv1_state,128*4); memcpy(out+384,st.fwc0_mem,164*4);
 memcpy(out+548,st.gru1_state,160*4); memcpy(out+708,st.gru2_state,128*4); memcpy(out+836,st.gru3_state,128*4);
}
'''
    generated = build / "reference_generated.c"
    generated.write_text(source)
    compiler = shutil.which("clang")
    subprocess.run([compiler, "-shared", "-O2", "-ffp-contract=off", "-D_CRT_SECURE_NO_WARNINGS",
                    "-I", str(HERE), "-o", str(build / "reference.dll"), str(generated)], check=True, cwd=build)
    dll = ctypes.CDLL(str(build / "reference.dll"))
    dll.rv_load.argtypes = [ctypes.c_char_p]
    for name, count in (("rv_prime", 1), ("rv_run", 2), ("rv_state", 1)):
        getattr(dll, name).argtypes = [ctypes.c_void_p]*count
    assert dll.rv_load(str(build / "reference_weights.bin").encode()) == 0
    return dll


def metrics(reference, output):
    d = np.asarray(reference) - np.asarray(output)
    return {"max_absolute_error": float(np.abs(d).max()), "rmse": float(np.sqrt(np.mean(d*d))),
            "reference_rms": float(np.sqrt(np.mean(np.asarray(reference)**2)))}


def main():
    torch.set_num_threads(1)
    dll = build_reference()
    model, init = Fargan().eval(), FarganInit().eval()
    shared = np.load(HERE.parents[1] / "rvtts_lpc/model/source_fixtures.npz")
    features = shared["case_5_acoustic_reference"]
    # The original wrapper adjusts pitch over the whole utterance.
    features = features.copy()
    pitch = features[:, 18]
    features[:, 18] = np.clip((pitch-pitch.mean())/max(float(pitch.std()),1e-5), -2.5, 2.5)*.3615+.2120
    x, last = frame_input(features[0])
    dll.rv_prime(features[0].ctypes.data)
    with torch.no_grad(): state = init(x, pitch_indices(last), torch.zeros(1, 964))
    cstate = np.zeros(964, np.float32); dll.rv_state(cstate.ctypes.data)
    init_error = metrics(cstate, state.numpy()[0])
    errors, references, outputs, data = [], [], [], []
    deemph = np.float32(0)
    with torch.no_grad():
        for frame in features[:40]:
            x, period = frame_input(frame)
            packed, indices = torch.cat((x, state), 1), pitch_indices(last)
            y = model(packed, indices)
            data.append((packed.numpy(), indices.numpy(), y.numpy()))
            state = y[:, 160:]
            pcm = y[0, :160].numpy().copy()
            for i in range(160):
                pcm[i] += np.float32(.85)*deemph; deemph = pcm[i]
            reference = np.zeros(160, np.float32); dll.rv_run(frame.ctypes.data, reference.ctypes.data)
            references.append(reference); outputs.append(pcm)
            last = period
    result = {"reference": "original rvTTS FARGAN neural/synthesis function bodies and LUT activations; float matrix kernels",
              "activation_change": "PyTorch uses standard tanh/sigmoid; original uses linearly interpolated LUTs",
              "init_state": init_error, "40_frame_pcm": metrics(np.concatenate(references), np.concatenate(outputs))}
    assert result["init_state"]["max_absolute_error"] < .01
    assert result["40_frame_pcm"]["max_absolute_error"] < .002
    assert result["40_frame_pcm"]["rmse"] < .0005
    (HERE / "float_equivalence.json").write_text(json.dumps(result, indent=2)+"\n")
    np.savez(HERE / "source_fixtures.npz", features=features, frame_inputs=np.concatenate([v[0] for v in data]),
             indices=np.concatenate([v[1] for v in data]), expected=np.concatenate([v[2] for v in data]),
             c_pcm=np.concatenate(references), torch_pcm=np.concatenate(outputs))
    weights = {k:v.detach().contiguous() for k,v in model.state_dict().items()}
    weights["frontend.pitch_embedding"] = torch.from_numpy(load_arrays()["cond_net_pembed_weights_float"].copy()).reshape(224,12)
    save_file(weights, HERE / "fargan_float.safetensors")
    print(result, flush=True)


if __name__ == "__main__":
    main()
