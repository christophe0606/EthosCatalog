# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Compile production C processing and verify independent NumPy/source fixtures.

MSVC: python tests/run_detection_tests.py --vcvars path/to/vcvars64.bat
Otherwise set --cc to a host Clang/GCC executable already in PATH.
"""
import argparse
import ctypes as ct
from pathlib import Path
import os
import subprocess
import sys
import numpy as np

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
sys.path.insert(0,str(ROOT/'projects/object_detection/model'))
from patterns import preprocess
from decode import decode
from common.stream_graph import configuration
from projects.object_detection.app import make_graph

class Box(ct.Structure):
    _fields_=[(x,ct.c_float) for x in ('x0','y0','x1','y1','score')]

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--vcvars',type=Path)
    parser.add_argument('--cc',default='clang')
    args=parser.parse_args()
    out=ROOT/'out/detection-tests'; out.mkdir(parents=True,exist_ok=True)
    project=ROOT/'projects/object_detection'
    common=ROOT/'common/nodes_impl/c'
    sources=[common/x for x in ('rgb565_center_crop.c','rgb888_to_gray_int8.c','non_maximum_suppression.c')]
    sources += [project/'nodes_impl'/x for x in ('face_decode.c','face_display.c')]
    library=out/('processing.dll' if os.name=='nt' else 'processing.so')
    exports=['rgb888_to_gray_int8','rgb565_center_crop','scored_box_nms','face_decode_candidates','face_decode','face_render']
    if args.vcvars:
        batch=out/'compile.cmd'
        batch.write_text('@echo off\ncall "'+str(args.vcvars.resolve())+'" >nul\n'
            +'cl /nologo /LD /O2 /fp:fast /TC /I"'+str(project)+'" '
            +' '.join('"'+str(s)+'"' for s in sources)
            +' /link /OUT:"'+str(library)+'" '+' '.join('/EXPORT:'+x for x in exports)+'\n')
        subprocess.run(['cmd','/d','/c',str(batch)],cwd=out,check=True)
    else:
        subprocess.run([args.cc,'-shared','-O3','-ffast-math','-fPIC','-I'+str(project),
            *map(str,sources),'-o',str(library)],check=True)
    lib=ct.CDLL(str(library))
    ptr=lambda a:a.ctypes.data_as(ct.c_void_p)
    lib.rgb888_to_gray_int8.argtypes=[ct.c_void_p,ct.c_uint32,ct.c_uint32,ct.c_uint32,ct.c_void_p]
    lib.rgb565_center_crop.argtypes=[ct.c_void_p,*[ct.c_uint32]*5,ct.c_void_p]
    lib.scored_box_nms.argtypes=[ct.c_void_p,ct.c_uint32,ct.c_float,ct.c_void_p,ct.c_void_p,ct.c_uint32,ct.POINTER(ct.c_uint32)]
    lib.face_decode_candidates.argtypes=[ct.c_void_p,ct.c_void_p,ct.c_void_p]
    lib.face_decode_candidates.restype=ct.c_uint32
    lib.face_decode.argtypes=[ct.c_void_p,ct.c_void_p,ct.c_void_p,ct.c_void_p]
    lib.face_decode.restype=ct.c_uint32
    lib.face_render.argtypes=[ct.c_void_p,ct.c_void_p,ct.c_void_p,ct.c_uint32]
    sample=np.frombuffer((project/'model/sample_image.rgb').read_bytes(),np.uint8).reshape(192,192,3)
    # All 16,777,216 RGB colors, chunked: catches float/FMA truncation boundaries.
    for red in range(256):
        g,b=np.mgrid[:256,:256]
        rgb=np.stack([np.full_like(g,red),g,b],axis=-1).astype(np.uint8)
        actual=np.empty((256,256),np.int8)
        assert lib.rgb888_to_gray_int8(ptr(rgb),256,256,256*3,ptr(actual))==0
        np.testing.assert_array_equal(actual,preprocess(rgb)[0,:,:,0])
    actual=np.empty((192,192),np.int8)
    assert lib.rgb888_to_gray_int8(ptr(sample),192,192,192*3,ptr(actual))==0
    np.testing.assert_array_equal(actual,preprocess(sample)[0,:,:,0])
    # Center crop and exact 565 bit expansion, with distinct channels/corners.
    raw=np.arange(640*480,dtype=np.uint16).reshape(480,640)
    rgb=np.empty((192,192,3),np.uint8)
    assert lib.rgb565_center_crop(ptr(raw),640,480,640,192,192,ptr(rgb))==0
    crop=raw[144:336,224:416]
    expected=np.stack([((crop>>11)<<3)|((crop>>11)>>2),
        (((crop>>5)&63)<<2)|(((crop>>5)&63)>>4),((crop&31)<<3)|((crop&31)>>2)],axis=-1).astype(np.uint8)
    np.testing.assert_array_equal(rgb,expected)
    # Different rectangular crop geometries and padded source rows, plus RGB
    # rows with padding; common contracts must not assume the detector's192².
    for w,h,stride,cw,ch in [(9,7,12,5,3),(8,6,10,4,4)]:
        raw=np.random.default_rng(w).integers(0,65536,(h,stride),dtype=np.uint16)
        rgb=np.empty((ch,cw,3),np.uint8)
        assert lib.rgb565_center_crop(ptr(raw),w,h,stride,cw,ch,ptr(rgb))==0
        crop=raw[(h-ch)//2:(h-ch)//2+ch,(w-cw)//2:(w-cw)//2+cw]
        expected=np.stack([((crop>>11)<<3)|((crop>>11)>>2),
            (((crop>>5)&63)<<2)|(((crop>>5)&63)>>4),((crop&31)<<3)|((crop&31)>>2)],axis=-1).astype(np.uint8)
        np.testing.assert_array_equal(rgb,expected)
        padded=np.full((ch,cw*3+5),201,np.uint8); padded[:,:cw*3]=rgb.reshape(ch,cw*3)
        gray=np.empty((ch,cw),np.int8)
        assert lib.rgb888_to_gray_int8(ptr(padded),cw,ch,padded.shape[1],ptr(gray))==0
        np.testing.assert_array_equal(gray,preprocess(rgb)[0,:,:,0])
        assert lib.rgb565_center_crop(ptr(raw),w,h,w-1,cw,ch,ptr(rgb))!=0
        assert lib.rgb888_to_gray_int8(ptr(padded),cw,ch,cw*3-1,ptr(gray))!=0
    # Stable ordering, unclipped coordinates, zero-area candidates, empty input,
    # exact IoU threshold semantics, and capacities independent of this model.
    for capacity,threshold,expected_x in [(4,0.,[5,-20,0]),(8,1.,[5,-20,0,0])]:
        candidates=(Box*4)(Box(0,0,10,10,.6),Box(5,0,15,10,.9),Box(-20,-20,-10,-10,.9),Box(0,0,0,0,.7))
        scratch=(Box*capacity)(); output=(Box*capacity)(); n=ct.c_uint32()
        before=bytes(candidates)
        assert lib.scored_box_nms(candidates,4,threshold,scratch,output,capacity,ct.byref(n))==0
        assert [output[i].x0 for i in range(n.value)]==expected_x
        assert bytes(candidates)==before and output[1].y0==-20
        assert lib.scored_box_nms(None,0,threshold,scratch,output,capacity,ct.byref(n))==0 and n.value==0
        assert lib.scored_box_nms(candidates,4,threshold,scratch,output,3,ct.byref(n))!=0
    work=(Box*540)(); boxes=(Box*540)()
    # Independently generated default TFLite outputs, including empty fixtures.
    import json
    fixtures=json.loads((project/'model/board_test_vectors.json').read_text())
    # Read actual record structure explicitly below to avoid coupling C tests to firmware arrays.
    cases=range(len(fixtures['patterns']))
    tested=0
    for case in cases:
        refs=[fixtures['expected'][f'default_{h}'][case] for h in range(2)]
        heads=[np.asarray(x,np.int8).reshape(1,g,g,18) for x,g in zip(refs,(6,12))]
        n=lib.face_decode(ptr(heads[0]),ptr(heads[1]),work,boxes)
        expected=decode(heads)
        assert n==len(expected)
        for i,e in enumerate(expected):
            np.testing.assert_allclose([boxes[i].x0,boxes[i].y0,boxes[i].x1,boxes[i].y1],e['box'],atol=.001,rtol=0)
            assert abs(boxes[i].score-e['score'])<1e-5
        # Exercise the graph's separate candidate/NMS path, then apply model
        # clipping independently; it must match the original combined adapter.
        candidates=(Box*540)(); output=(Box*540)(); nms_work=(Box*540)(); count=ct.c_uint32()
        nc=lib.face_decode_candidates(ptr(heads[0]),ptr(heads[1]),candidates)
        assert lib.scored_box_nms(candidates,nc,.45,nms_work,output,540,ct.byref(count))==0 and count.value==n
        for i in range(n):
            coords=np.clip([output[i].x0,output[i].y0,output[i].x1,output[i].y1],0,192)
            np.testing.assert_array_equal(coords,[boxes[i].x0,boxes[i].y0,boxes[i].x1,boxes[i].y1])
        tested+=1
    # Synthetic overlapping, boundary and all-candidate cases compare source NMS.
    rng=np.random.default_rng(19)
    for k in range(8):
        heads=[np.full((1,g,g,18),-128,np.int8) for g in (6,12)]
        if k:
            for h,zero in zip(heads,(47,10)):
                grid=h.shape[1]
                for y,x,a in ((0,0,0),(grid-1,0,0),(0,grid-1,1),(grid-1,grid-1,2),(grid//2,grid//2,0),(grid//2,grid//2,1)):
                    q=h[0,y,x,a*6:(a+1)*6]
                    q[:4]=zero+rng.integers(-8,9,4)
                    q[4:]=zero+rng.integers(9,25,2)
        n=lib.face_decode(ptr(heads[0]),ptr(heads[1]),work,boxes)
        expected=decode(heads)
        assert n==len(expected),(k,n,len(expected))
        for i,e in enumerate(expected):
            np.testing.assert_allclose([boxes[i].x0,boxes[i].y0,boxes[i].x1,boxes[i].y1],e['box'],atol=.003,rtol=0)
    # Exercise all 540 candidates without truncating a fixed top-k list.
    heads=[np.zeros((1,g,g,18),np.int8) for g in (6,12)]
    for h,zero in zip(heads,(47,10)):
        h[:]=zero; h[...,4::6]=zero+20; h[...,5::6]=zero+20
    n=lib.face_decode(ptr(heads[0]),ptr(heads[1]),work,boxes)
    assert 0<n<=540
    # Rotated preview corners and boxes touching [0,192] cannot corrupt margins.
    image=np.zeros((192,192,3),np.uint8)
    image[0,0]=[255,0,0]; image[0,191]=[0,255,0]; image[191,0]=[0,0,255]; image[191,191]=255
    guard=np.full(480*800+64,0x1234,np.uint16); lcd=guard[32:-32].reshape(800,480); lcd[:]=0
    lib.face_render(ptr(lcd),ptr(image),boxes,0)
    assert lcd[208,430]==0xf800 and lcd[590,430]==0x07e0
    assert lcd[208,48]==0x001f and lcd[590,48]==0xffff
    # Interpret physical scanout through the one landscape transform.
    logical=lcd[:,::-1].T
    assert logical.shape==(480,800)
    assert logical[48,208]==0xf800 and logical[48,590]==0x07e0
    assert logical[430,208]==0x001f and logical[430,590]==0xffff
    boundary=(Box*1)(Box(-30,-10,220,240,1))
    lib.face_render(ptr(lcd),ptr(image),boundary,1)
    assert (guard[:32]==0x1234).all() and (guard[-32:]==0x1234).all()
    assert (lcd[:208]==0).all() and (lcd[592:]==0).all()
    assert (lcd[208:592,:48]==0).all() and (lcd[208:592,432:]==0).all()
    preview=logical[48:432,208:592]
    assert (preview[0,:]==0xf800).all() and (preview[-1,:]==0xf800).all()
    assert (preview[:,0]==0xf800).all() and (preview[:,-1]==0xf800).all()
    sched=make_graph().computeSchedule(config=configuration())
    names=[sched.allNodes[i].nodeName for i in sched.schedule]
    assert names==['camera','crop','dup0','gray','network','decode','nms','display'],names
    assert sched.scheduleLength==8 and sched.memory==72
    includes=[ROOT/'tests/detection',project,project/'generated',project/'nodes_impl',
        ROOT/'board/DevKit-E8',ROOT/'thirdparty/CMSIS-Stream/platform',
        ROOT/'thirdparty/CMSIS-Stream/platform/posix_runtime',common]
    exe=out/('nodes.exe' if os.name=='nt' else 'nodes')
    if args.vcvars:
        batch.write_text('@echo off\ncall "'+str(args.vcvars.resolve())+'" >nul\n'
            +'cl /nologo /EHsc /std:c++17 /O2 '
            +' '.join('/I"'+str(p)+'"' for p in includes)
            +' "'+str(ROOT/'tests/test_detection_nodes.cpp')+'" '+' '.join(s.stem+'.obj' for s in sources)+' '
            +'/Fe:"'+str(exe)+'"\n')
        subprocess.run(['cmd','/d','/c',str(batch)],cwd=out,check=True)
    else:
        objects=[]
        for source in sources:
            obj=out/(source.stem+'.o'); objects.append(obj)
            subprocess.run([args.cc,'-O3','-ffast-math','-I'+str(project),'-c',str(source),'-o',str(obj)],check=True)
        subprocess.run([args.cc+'++','-std=c++17','-O2',*['-I'+str(p) for p in includes],
            str(ROOT/'tests/test_detection_nodes.cpp'),*map(str,objects),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
    # Exercise the actual hardware adapter with a pack-driver contract double:
    # Stop erases CPI configuration/interrupt enables on Ensemble 2.2.1.
    camera_includes=ROOT/'tests/detection/camera'
    camera_sources=[ROOT/'tests/test_detection_camera.c',ROOT/'common/nodes_impl/alif_e8/camera.c']
    camera_exe=out/('camera.exe' if os.name=='nt' else 'camera')
    rte=ROOT/'board/DevKit-E8/RTE/Device/AE822FA0E5597LS0_M55_HP'
    profile=ROOT/'tests/test_detection_board_profiles.c'
    if args.vcvars:
        batch.write_text('@echo off\ncall "'+str(args.vcvars.resolve())+'" >nul\n'
            +'cl /nologo /TC /O2 /I"'+str(camera_includes)+'" '
            +' '.join('"'+str(s)+'"' for s in camera_sources)+' /Fe:"'+str(camera_exe)+'"\n'
            +'if errorlevel 1 exit /b 1\n'
            +'cl /nologo /TC /Zs /I"'+str(rte)+'" "'+str(profile)+'"\n'
            +'if errorlevel 1 exit /b 1\n'
            +'cl /nologo /TC /Zs /DOBJECT_DETECTION_RGB565 /I"'+str(rte)+'" "'+str(profile)+'"\n')
        subprocess.run(['cmd','/d','/c',str(batch)],cwd=out,check=True)
    else:
        subprocess.run([args.cc,'-O2','-I'+str(camera_includes),*map(str,camera_sources),'-o',str(camera_exe)],check=True)
        for defines in ([],['-DOBJECT_DETECTION_RGB565']):
            subprocess.run([args.cc,'-fsyntax-only','-I'+str(rte),*defines,str(profile)],check=True)
    subprocess.run([str(camera_exe)],check=True)
    # Existing spectrum projects retain the full LCD+touch initializer.
    assert 'return lcd_init(framebuffers[0]);' in (ROOT/'projects/template_mram/hardware_params.cpp').read_text()
    lcd=(ROOT/'common/nodes_impl/alif_e8/lcd.c').read_text()
    assert 'CHECK(lcd_init_display(initial_frame));' in lcd and 'CHECK(GT911.Initialize());' in lcd
    print(f'PASS: all RGB colors, multiple crop/stride/NMS configurations, {tested} reference fixtures, decoder/NMS, landscape clipping, seven application nodes plus view duplicate')

if __name__=='__main__': main()
