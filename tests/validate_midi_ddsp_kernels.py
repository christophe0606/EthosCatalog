#!/usr/bin/env python3
# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Compile production float loops and compare normalization against PyTorch."""
import argparse
import ctypes
import hashlib
import json
import subprocess
import tempfile
from pathlib import Path
import numpy as np
import torch

ROOT = Path(__file__).resolve().parents[1]
KERNELS = ROOT / 'projects/midi_ddsp/kernels'
WRAPPER = r'''
#include "float_loops.hpp"
#include "quantization_loops.hpp"
using namespace midi_ddsp::kernels;
#ifdef _WIN32
#define EXPORT extern "C" __declspec(dllexport)
#else
#define EXPORT extern "C"
#endif
EXPORT void apply_binary(const float *a, size_t na, const float *b, size_t nb,
                         float *out, size_t n, int op, float alpha) {
    binary(a,na,b,nb,out,n,static_cast<Binary>(op),alpha);
}
EXPORT void apply_mean(const float *a,float *out,size_t rows,size_t columns) {
    mean_tail(a,out,rows,columns);
}
EXPORT void apply_square(const float *a,float *out,size_t n) { square(a,out,n); }
EXPORT void apply_rsqrt(const float *a,float *out,size_t n) { reciprocal_sqrt(a,out,n); }
EXPORT int apply_quantize(const float *a,int16_t *out,size_t n,float inverse,int16_t lo,int16_t hi) {
    return quantize_short(a,out,n,inverse,lo,hi);
}
EXPORT void apply_dequantize(const int16_t *a,float *out,size_t n,float scale) {
    dequantize_short(a,out,n,scale);
}
EXPORT void apply_rows(const float *full,const float *rows,float *out,size_t n,size_t inner,
                       int rows_first,int op,float alpha) {
    binary_rows(full,rows,out,n,inner,bool(rows_first),static_cast<Binary>(op),alpha);
}
EXPORT size_t eligible_rows(const int64_t *a,int ar,const int64_t *b,int br) {
    Shape x{},y{};x.rank=ar;y.rank=br;
    for (int j=0;j<ar;++j) x.extent[j]=a[j];
    for (int j=0;j<br;++j) y.extent[j]=b[j];
    return row_broadcast_inner(x,y);
}
EXPORT int eligible(const int64_t *a,int ar,const int64_t *b,int br) {
    Shape x{},y{}; x.rank=ar;y.rank=br;
    for (int j=0;j<ar;++j) x.extent[j]=a[j];
    for (int j=0;j<br;++j) y.extent[j]=b[j];
    return repeated_suffix(x,y);
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--vcvars', type=Path, required=True)
    parser.add_argument('--pack-root', type=Path, help='Optional installed CMSIS pack root for ExecuTorch wrapper compilation.')
    parser.add_argument('--rtos-include', type=Path, help='Optional CMSIS RTOS2 Include directory to compile quantization profiling wrappers.')
    parser.add_argument('--output', type=Path, default=ROOT/'projects/midi_ddsp/model/normalization_validation.json')
    args = parser.parse_args()
    testroot = ROOT/'out/midi-ddsp-kernel-tests'
    testroot.mkdir(parents=True, exist_ok=True)
    # Windows retains loaded DLL mappings; isolate builds so a preceding run's
    # import library or debugger cannot prevent the next validation build.
    outdir = Path(tempfile.mkdtemp(prefix='run-',dir=testroot))
    source = outdir/'wrapper.cpp'
    source.write_text(WRAPPER)
    library = outdir/'float_loops.dll'
    batch = outdir/'compile.cmd'
    batch.write_text('@echo off\ncall "'+str(args.vcvars.resolve())+'" >nul\n'
                     'cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /LD /I"'+str(KERNELS)+'" '
                     '"'+str(source)+'" /link /OUT:"'+str(library)+'"\n')
    subprocess.run(['cmd','/d','/c',str(batch)],cwd=outdir,check=True)
    if args.pack_root:
        et = args.pack_root/'PyTorch/ExecuTorch/1.4.1/include'
        batch.write_text('@echo off\ncall "'+str(args.vcvars.resolve())+'" >nul\n'
                         'cl /nologo /std:c++17 /EHsc /W3 /O2 /c /DMIDI_DDSP_NORMALIZATION_TIMING=0 /DC10_USING_CUSTOM_GENERATED_MACROS /DC10_BUILD_MAIN_LIB /I"'+str(et)+'" /I"'+str(ROOT/'common')+'" '
                         '"'+str(KERNELS/'normalization.cpp')+'" /Fo:"'+str(outdir/'normalization.obj')+'"\n')
        subprocess.run(['cmd','/d','/c',str(batch)],cwd=outdir,check=True)
        if args.rtos_include:
            batch.write_text('@echo off\ncall "'+str(args.vcvars.resolve())+'" >nul\n'
                             'cl /nologo /std:c++17 /EHsc /W3 /O2 /c /DC10_USING_CUSTOM_GENERATED_MACROS /DC10_BUILD_MAIN_LIB /I"'+str(et)+'" /I"'+str(args.rtos_include)+'" '
                             '"'+str(KERNELS/'quantization.cpp')+'" /Fo:"'+str(outdir/'quantization.obj')+'"\n')
            subprocess.run(['cmd','/d','/c',str(batch)],cwd=outdir,check=True)
    dll = ctypes.CDLL(str(library))
    fp = np.ctypeslib.ndpointer(dtype=np.float32, flags='C_CONTIGUOUS')
    ip = np.ctypeslib.ndpointer(dtype=np.int64, flags='C_CONTIGUOUS')
    sp = np.ctypeslib.ndpointer(dtype=np.int16, flags='C_CONTIGUOUS')
    size = ctypes.c_size_t
    dll.apply_binary.argtypes = [fp,size,fp,size,fp,size,ctypes.c_int,ctypes.c_float]
    dll.apply_mean.argtypes = [fp,fp,size,size]
    dll.apply_square.argtypes = [fp,fp,size]
    dll.apply_rsqrt.argtypes = [fp,fp,size]
    dll.apply_quantize.argtypes = [fp,sp,size,ctypes.c_float,ctypes.c_int16,ctypes.c_int16]
    dll.apply_quantize.restype = ctypes.c_int
    dll.apply_dequantize.argtypes = [sp,fp,size,ctypes.c_float]
    dll.apply_rows.argtypes = [fp,fp,fp,size,size,ctypes.c_int,ctypes.c_int,ctypes.c_float]
    dll.eligible_rows.argtypes = [ip,ctypes.c_int,ip,ctypes.c_int]
    dll.eligible_rows.restype = size
    dll.eligible.argtypes = [ip,ctypes.c_int,ip,ctypes.c_int]
    dll.eligible.restype = ctypes.c_int
    rng = np.random.default_rng(9100)
    cases = []
    quantization_cases = []

    # ExecuTorch's scalar Short path multiplies by a float32 reciprocal and
    # nearbyint rounds ties to even. Division in float64 is a different oracle.
    for scale in (1.,.000123,1e-8,.37,1000.):
        inverse = np.float32(1)/np.float32(scale)
        half_values = np.arange(-32770,32771,dtype=np.float32)+np.float32(.5)
        inputs = np.concatenate((half_values/inverse,
            np.nextafter(half_values/inverse,np.float32(-np.inf)),
            np.nextafter(half_values/inverse,np.float32(np.inf)),
            rng.uniform(-1e5,1e5,2048).astype(np.float32)/inverse))
        for lo,hi in ((-32768,32767),(-32767,32767),(-123,456)):
            actual = np.empty(inputs.size,np.int16)
            assert dll.apply_quantize(inputs,actual,inputs.size,inverse,lo,hi)
            expected = np.clip(np.rint(inputs*inverse).astype(np.int64),lo,hi).astype(np.int16)
            np.testing.assert_array_equal(actual,expected)
            quantization_cases.append({'scale':scale,'limits':[lo,hi],'elements':inputs.size})
    # Remainders and guards are separate from the large numerical fixture.
    for n in (*range(20),127,128,129):
        source = rng.normal(size=n).astype(np.float32)
        actual = np.empty(n,np.int16)
        assert dll.apply_quantize(source,actual,n,1.,-32768,32767)
        np.testing.assert_array_equal(actual,np.rint(source).astype(np.int16))
    for bad in (np.nan,np.inf,-np.inf,np.float32(2**63)):
        source = np.array([0,1,2,3,bad],np.float32)
        assert not dll.apply_quantize(source,np.empty(5,np.int16),5,1.,-32768,32767)
    signed_values = np.arange(-32768,32768,dtype=np.int32).astype(np.int16)
    for scale in (1.,.000123,1e-8,.37,1000.):
        actual = np.empty(signed_values.size,np.float32)
        dll.apply_dequantize(signed_values,actual,signed_values.size,np.float32(scale))
        np.testing.assert_array_equal(actual.view(np.uint32),
            (signed_values.astype(np.float32)*np.float32(scale)).view(np.uint32))
        quantization_cases.append({'dequantize_scale':scale,'elements':signed_values.size})

    def check(name, actual, expected, atol=3e-5, rtol=3e-5):
        actual, expected = np.asarray(actual), np.asarray(expected)
        np.testing.assert_allclose(actual,expected,atol=atol,rtol=rtol,err_msg=name)
        cases.append({'case':name,'max_absolute_error':float(np.max(np.abs(actual-expected)))})

    def binary(a,b,op,alpha=1,alias=False):
        shape = np.broadcast_shapes(a.shape,b.shape)
        result = a if alias else np.empty(shape,np.float32)
        assert not alias or a.shape == shape
        dll.apply_binary(a,a.size,b,b.size,result,result.size,op,alpha)
        return result

    # These include global time+channel moments, per-frame layer moments,
    # cancellation, singleton/remainder paths and the largest deployed shape.
    for rows,columns in [(1,1),(3,7),(2,128),(1,256),(1,512),(1000,128),(1,128000)]:
        for kind in ('ordinary','near_constant','large_offset'):
            x = rng.normal(size=(rows,columns)).astype(np.float32)
            if kind == 'near_constant': x = x*1e-4+2
            if kind == 'large_offset': x += 1000
            ref = torch.from_numpy(x)
            mean = np.empty((rows,1),np.float32)
            dll.apply_mean(x,mean,rows,columns)
            check(f'mean_{rows}_{columns}_{kind}',mean,ref.mean(-1,keepdim=True).numpy(),atol=2e-3 if kind=='large_offset' else 3e-6,rtol=3e-6)
            centered = binary(x,mean,1) if rows==1 else np.concatenate([binary(x[r:r+1],mean[r:r+1],1) for r in range(rows)])
            squared = np.empty_like(x); dll.apply_square(centered,squared,x.size)
            variance = np.empty_like(mean); dll.apply_mean(squared,variance,rows,columns)
            eps = np.array(1e-5,np.float32)
            ve = binary(variance,eps,0)
            inv = np.empty_like(ve); dll.apply_rsqrt(ve,inv,ve.size)
            normalized = binary(centered,inv,2) if rows==1 else np.concatenate([binary(centered[r:r+1],inv[r:r+1],2) for r in range(rows)])
            expected = (ref-ref.mean(-1,keepdim=True))*torch.rsqrt(((ref-ref.mean(-1,keepdim=True))**2).mean(-1,keepdim=True)+1e-5)
            # Large offsets amplify reduction-order differences; report them
            # separately, while normal deployment-scale cases have a tight gate.
            # Near-constant values amplify one float32 mean ULP by 1/sqrt(eps).
            atol = 8e-4 if kind=='large_offset' else 2e-4 if kind=='near_constant' else 6e-5
            check(f'normalization_{rows}_{columns}_{kind}',normalized,expected.numpy(),atol=atol,rtol=3e-5)

    for shape,small in [((2,17,128),(128,)),((2,17,128),(17,128)),((2,17,128),()),((1,1000,128),(1,1,128))]:
        a = rng.normal(size=shape).astype(np.float32)
        b = rng.normal(size=small).astype(np.float32)
        for op in range(3):
            for reverse in (False,True):
                x,y = (b,a) if reverse else (a,b)
                alpha = .375
                expected = x+alpha*y if op==0 else x-alpha*y if op==1 else x*y
                check(f'broadcast_{shape}_{small}_{op}_{reverse}',binary(x,y,op,alpha),expected)
        check(f'inplace_{shape}_{small}',binary(a.copy(),b,1,alias=True),a-b)
    for small,large,expected in [((128,),(2,17,128),1),((1,1,128),(1,1000,128),1),
                                  ((17,1),(2,17,128),0),((128,),(1,1000,64),0),
                                  ((),(1,1000,128),1),((1,17,128),(2,17,128),1)]:
        actual = dll.eligible(np.array(small,np.int64),len(small),np.array(large,np.int64),len(large))
        assert actual==expected,(small,large,actual,expected)
    for outer,inner in ((16,512),(3,7),(1,1),(2,128)):
        full=rng.normal(size=(outer,inner)).astype(np.float32)
        rows=rng.normal(size=(outer,1)).astype(np.float32)
        for reverse in (0,1):
            for op in range(3):
                actual=np.empty_like(full)
                dll.apply_rows(full,rows,actual,actual.size,inner,reverse,op,.375)
                x,y=(rows,full) if reverse else (full,rows)
                expected=x+.375*y if op==0 else x-.375*y if op==1 else x*y
                check(f'row_broadcast_{outer}_{inner}_{reverse}_{op}',actual,expected)
        alias=full.copy()
        dll.apply_rows(alias,rows,alias,alias.size,inner,0,1,1.)
        check(f'row_inplace_{outer}_{inner}',alias,full-rows)
    for small,large,expected in [((16,1),(16,512),512),((2,1,1),(2,17,128),2176),
        ((1,16,1),(1,16,512),512),((1,16,1),(2,16,512),0),
        ((16,),(16,512),0),((3,1),(2,7),0),((2,7),(2,7),1)]:
        actual=dll.eligible_rows(np.array(small,np.int64),len(small),np.array(large,np.int64),len(large))
        assert actual==expected,(small,large,actual,expected)
    report = {'passed':True,'cases':cases,'shape_selection_cases':6,
              'row_broadcast_shape_cases':7,
              'quantization_cases':quantization_cases,'quantization_tail_cases':23,'quantization_nonfinite_guard_cases':4,
              'executorch_wrapper_compile_checked': bool(args.pack_root),
              'quantization_wrapper_compile_checked': bool(args.pack_root and args.rtos_include),
              'scope':'Production scalar float and Short loops compiled by host MSVC; independent PyTorch normalization and NumPy quantization oracles. ExecuTorch wrapper selection, target fast-math/MVE behavior and complete neural outputs still require board validation.',
              'source_sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in KERNELS.glob('*') if p.suffix in ('.cpp','.hpp')}}
    args.output.write_text(json.dumps(report,indent=2)+'\n')
    print('PASS',len(cases),'normalization numerical cases,6shape cases,',len(quantization_cases),'quantization cases,23tails,4guards; maxerror',max(c['max_absolute_error'] for c in cases),flush=True)


if __name__=='__main__': main()
