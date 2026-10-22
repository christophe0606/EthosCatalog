#!/usr/bin/env python3
# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Compare float/PTQ handwriting with the production C++ recurrence and RNG.

Uses saved pre-Vela quantized graphs; it does not rebuild/requantize the model.
PNG/SVG previews show unwrapped trajectories, independently of LCD layout.
"""
import argparse
import ctypes as ct
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import zlib

import numpy as np

ROOT = Path(__file__).resolve().parents[3]
DIRECTORY = Path(__file__).resolve().parent


def png(path, pixels):
    def chunk(tag, data):
        return struct.pack('>I',len(data))+tag+data+struct.pack('>I',zlib.crc32(tag+data)&0xffffffff)
    height, width = pixels.shape[:2]
    scan = b''.join(b'\0'+line.tobytes() for line in pixels)
    path.write_bytes(b'\x89PNG\r\n\x1a\n'
        +chunk(b'IHDR',struct.pack('>IIBBBBB',width,height,8,0 if pixels.ndim==2 else 2,0,0,0))
        +chunk(b'IDAT',zlib.compress(scan))+chunk(b'IEND',b''))


def build_bridge(output, vcvars, cxx):
    sources = [ROOT/'tests/handwriting/quality_engine.cpp',
               DIRECTORY.parent/'nodes_impl/handwriting_engine.cpp']
    include = DIRECTORY.parent/'nodes_impl'
    # Windows can hold a loaded DLL/import library open. Keep builds isolated
    # by source content; a rerender never needs to overwrite an earlier bridge.
    digest=hashlib.sha256()
    for path in [*sources,*sorted(include.glob('handwriting_*.hpp'))]:
        digest.update(path.read_bytes())
    output=output/'bridge'/digest.hexdigest()[:16]
    output.mkdir(parents=True,exist_ok=True)
    library = output/('quality_engine.dll' if sys.platform == 'win32' else 'quality_engine.so')
    if library.is_file():
        return ct.CDLL(str(library))
    if vcvars:
        batch = output/'compile_quality.cmd'
        batch.write_text('@echo off\ncall "'+str(vcvars.resolve())+'" >nul\n'
                         'cl /nologo /std:c++17 /EHsc /O2 /fp:fast /LD '
                         +f'/I"{include}" '+ ' '.join(f'"{p}"' for p in sources)
                         +f' /link /OUT:"{library}"\n')
        subprocess.run(['cmd', '/d', '/c', str(batch)], cwd=output, check=True)
    else:
        subprocess.run([cxx, '-std=c++17', '-O2', '-ffast-math', '-shared',
                        '-fPIC', f'-I{include}', *map(str, sources), '-o', str(library)], check=True)
    return ct.CDLL(str(library))


def preview(output, name, trajectories):
    """Standalone previews, using only NumPy and the standard library."""
    from html import escape
    width, row_height, oversample = 1500, 160, 3
    canvas = np.zeros((len(trajectories)*row_height*oversample, width*oversample), dtype=np.uint8)
    paths = []
    for row, (label, points) in enumerate(trajectories):
        if not len(points):
            continue
        coords = np.cumsum(points[:, :2].astype(np.float64), axis=0)
        low, high = coords.min(0), coords.max(0)
        scale = min((width-40)/max(high[0]-low[0], 1), 105/max(high[1]-low[1], 1), 4)
        coords = (coords-low)*scale
        coords[:, 0] += 20
        coords[:, 1] = row*row_height+125-coords[:, 1]
        previous_up, commands = True, []
        for i, ((x, y), pen) in enumerate(zip(coords, points[:, 2])):
            commands.append(f'{"M" if previous_up else "L"}{x:.3f},{y:.3f}')
            if not previous_up:
                start = coords[i-1]*oversample
                end = coords[i]*oversample
                count = max(1, int(np.max(np.abs(end-start)))+1)
                pixels = np.rint(np.linspace(start, end, count)).astype(int)
                # Thin, antialiased preview. The LCD has its own renderer.
                for ox in range(-2, 3):
                    for oy in range(-2, 3):
                        xx, yy = pixels[:, 0]+ox, pixels[:, 1]+oy
                        valid = (xx>=0)&(xx<canvas.shape[1])&(yy>=0)&(yy<canvas.shape[0])
                        canvas[yy[valid], xx[valid]] = 255
            previous_up = pen>=.5
        paths.append(f'<text x="20" y="{row*row_height+18}" fill="white">{escape(label)}</text>'
                     f'<path d="{" ".join(commands)}" fill="none" stroke="white" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"/>')
    (output/(name+'.svg')).write_text(
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{len(trajectories)*row_height}">'
        '<rect width="100%" height="100%" fill="black"/>'+''.join(paths)+'</svg>')
    canvas = canvas.reshape(len(trajectories)*row_height, oversample, width, oversample).mean((1, 3)).astype(np.uint8)
    png(output/(name+'.png'),canvas)


def lcd_previews(library, output):
    """Render cached traces; changing the pen does not require new inference."""
    pointer = ct.POINTER(ct.c_float)
    library.quality_render.argtypes = [pointer,pointer,ct.c_uint,ct.c_char_p,ct.c_uint,ct.POINTER(ct.c_uint16)]
    library.quality_render.restype = None
    report = json.loads((output/'comparison.json').read_text())
    texts = list(dict.fromkeys(record['text'] for record in report['results']))
    with np.load(output/'trajectories.npz') as traces:
        for text_index,text in enumerate(texts):
            rows = []
            for bias,mode,label in [(.5,0,'before'),(1.,1,'after')]:
                key = f'text{text_index}_seed42_bias{bias:g}_quantized'
                if key not in traces:
                    continue
                points = np.ascontiguousarray(traces[key],dtype=np.float32)
                attention = np.ascontiguousarray(traces[key+'_attention'],dtype=np.float32)
                frame = np.zeros((480,800),dtype=np.uint16)
                library.quality_render(points.ctypes.data_as(pointer),attention.ctypes.data_as(pointer),
                                       len(points),text.encode('ascii'),mode,frame.ctypes.data_as(ct.POINTER(ct.c_uint16)))
                wide = frame.astype(np.uint32)
                rgb = np.stack([((wide>>11)&31)*255//31,((wide>>5)&63)*255//63,(wide&31)*255//31],axis=-1).astype(np.uint8)
                png(output/f'lcd_{text_index}_{label}.png',rgb)
                rows.append(rgb[:150])
            if rows:
                png(output/f'lcd_{text_index}_before_after.png',np.concatenate(rows))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--vcvars', type=Path)
    parser.add_argument('--cxx', default='clang++')
    parser.add_argument('--output', type=Path, default=ROOT/'out/handwriting-quality')
    parser.add_argument('--sentences', nargs='+', default=['abcdefgh', 'This is not working very well ...'])
    parser.add_argument('--biases', nargs='+', type=float, default=[.5, .75, 1., 1.25])
    parser.add_argument('--seeds', nargs='+', type=int, default=[42])
    parser.add_argument('--render-only', action='store_true', help='Render existing saved traces with the current LCD pen')
    args = parser.parse_args()
    output = args.output.resolve(); output.mkdir(parents=True, exist_ok=True)
    library = build_bridge(output, args.vcvars, args.cxx)
    if args.render_only:
        lcd_previews(library,output);return
    import torch
    import torchao.quantization.pt2e.quantize_pt2e  # Register exported PTQ ops.
    from model import load_models, SHAPES, encode
    torch.set_num_threads(1)
    float_models = load_models()
    quantized, ranges = {}, {}
    for method in SHAPES:
        program = torch.export.load(DIRECTORY/(method+'_quantized.pt2'))
        quantized[method] = program.module(check_guards=False)
        nodes = [n for n in program.graph.nodes if n.target==torch.ops.quantized_decomposed.quantize_per_tensor.default]
        ranges[method] = [float(n.args[1])*32767 for n in (nodes[0], nodes[-1])]
    pointer = ct.POINTER(ct.c_float)
    callback_type = ct.CFUNCTYPE(ct.c_int, ct.c_uint, pointer, pointer)
    library.quality_begin.argtypes = [callback_type, ct.c_char_p, ct.c_uint, ct.c_float]
    library.quality_begin.restype = ct.c_int
    library.quality_step.argtypes = [pointer, pointer]
    library.quality_step.restype = ct.c_int
    results, saved = [], {}
    for text_index, text in enumerate(args.sentences):
        encode(text)  # Validate before crossing the FFI boundary.
        previews = []
        for seed in args.seeds:
            for bias in args.biases:
                for kind, models in [('float', float_models), ('quantized', quantized)]:
                    stats = {name:dict(input_clipped=0, output_out_of_range=0, max_abs_error=0.,
                                      squared_error=0., squared_reference=0., elements=0)
                             for name in SHAPES}
                    failures = []
                    @callback_type
                    def dense(method, source, destination):
                        try:
                            name = list(SHAPES)[method]
                            ni, no = SHAPES[name]
                            x = torch.from_numpy(np.ctypeslib.as_array(source, shape=(ni,))).reshape(1, ni)
                            with torch.no_grad():
                                y = models[name](x)
                                if kind=='quantized':
                                    reference = float_models[name](x)
                                    difference = y-reference
                                    s = stats[name]
                                    s['input_clipped'] += int((x.abs()>ranges[name][0]).sum())
                                    s['output_out_of_range'] += int((reference.abs()>ranges[name][1]).sum())
                                    s['max_abs_error'] = max(s['max_abs_error'],float(difference.abs().max()))
                                    s['squared_error'] += float(difference.square().sum())
                                    s['squared_reference'] += float(reference.square().sum())
                                    s['elements'] += no
                            np.copyto(np.ctypeslib.as_array(destination, shape=(no,)),y.detach().numpy().reshape(-1))
                            return 0
                        except Exception as error:
                            failures.append(repr(error)); return -999
                    status = library.quality_begin(dense, text.encode('ascii'), seed, bias)
                    if status:
                        raise RuntimeError(f'begin failed: {status}')
                    points, attention = [], []
                    point, phi = np.zeros(3, np.float32), np.zeros(len(text)+1, np.float32)
                    while True:
                        status = library.quality_step(point.ctypes.data_as(pointer),phi.ctypes.data_as(pointer))
                        if status:
                            break
                        points.append(point.copy()); attention.append(phi.copy())
                    if status<0:
                        raise RuntimeError((status, failures))
                    points, attention = np.asarray(points), np.asarray(attention)
                    key = f'text{text_index}_seed{seed}_bias{bias:g}_{kind}'
                    saved[key] = points; saved[key+'_attention'] = attention
                    record = dict(text=text, seed=seed, bias=bias, model=kind, points=len(points),
                                  termination='eos' if status==1 else 'step_limit',
                                  finite=bool(np.isfinite(points).all()))
                    if kind=='quantized':
                        for s in stats.values():
                            s['normalized_rmse_percent'] = 100*np.sqrt(s.pop('squared_error')/max(s.pop('squared_reference'),1e-30))
                        record['dense_error_on_quantized_trajectory'] = stats
                    results.append(record)
                    previews.append((f'{kind}, bias={bias:g}, seed={seed}, {len(points)} points, {record["termination"]}: {text}',points))
                    print({k:v for k,v in record.items() if k!='dense_error_on_quantized_trajectory'},flush=True)
        preview(output,f'comparison_{text_index}',previews)
    np.savez(output/'trajectories.npz',**saved)
    provenance = {p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in [
        DIRECTORY.parent/'nodes_impl/handwriting_engine.cpp', ROOT/'tests/handwriting/quality_engine.cpp',
        *[DIRECTORY/(name+'_quantized.pt2') for name in SHAPES]]}
    (output/'comparison.json').write_text(json.dumps(dict(
        description='Production C++ recurrence, xorshift RNG and sampler; float/PTQ dense layers on host. '
                    'Pre-Vela PTQ is not bit-identical to the NPU; no OCR score. PNG rows follow JSON order per text; SVG includes labels.',
        provenance=provenance,results=results),indent=2)+'\n')
    lcd_previews(library,output)


if __name__=='__main__':
    main()
