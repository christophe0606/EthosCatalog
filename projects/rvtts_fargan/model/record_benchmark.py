#!/usr/bin/env python3
# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Record schema-valid FARGAN measurements, preserving numerical failures."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import statistics as stats
import yaml

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
SHAPES={"prosody_pre":([32,184],[32,64]),"prosody_gru":([1,256],[1,128]),
        "prosody_head":([32,128],[32,6]),"acoustic":([1,100,170],[1,32,17]),
        "fargan_init":([1,32],[1,964]),"fargan_frame":([1,996],[1,1124])}

def digest(path): return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--recorded-at",required=True)
    p.add_argument("--build-job",required=True)
    p.add_argument("--execution-job",required=True)
    p.add_argument("--firmware",type=Path,default=ROOT/"out/rvtts_fargan/DevKit-E8/Release/rvtts_fargan.axf")
    args=p.parse_args()
    text=(HERE/"benchmark_uart.txt").read_text()
    assert text.count("benchmark clocks")==1 and "Test_result: FAIL" in text and "Numeric_failures: 2" in text
    cpu,axi,timer,model_bytes=map(int,re.search(r"CPU=(\d+) AXI=(\d+) timer=(\d+) model=(\d+)",text).groups())
    method_peak,temp_peak,reserved=map(int,re.search(r"Pools method=(\d+) temp=(\d+) reserved=(\d+)",text).groups())
    details={"recorded_at":args.recorded_at,"methods":{},"sequences":{},"full_frames":{},
             "model_sha256":digest(HERE.parent/"ai_layer/model.pte"),"firmware_sha256":digest(args.firmware),
             "pools":{"method_peak_bytes":method_peak,"temp_peak_bytes":temp_peak,"reserved_bytes":reserved},
             "status":"EXPERIMENTAL: numerical checks fail for both FARGAN methods; measured synthesis exceeds real-time budget."}
    for name,error,tolerance,weights,arena in re.findall(r"Method (\w+) error=([\d.]+) tolerance=([\d.]+) weights=(\d+) arena=(\d+)",text):
        details["methods"][name]=dict(max_absolute_error=float(error),tolerance=float(tolerance),weights_region_bytes=int(weights),arena_region_bytes=int(arena))
    for n,samples,audio_ms,error in re.findall(r"Sequence frames=(\d+) samples=(\d+) audio_ms=(\d+) max_error=([\d.]+)",text):
        details["sequences"][n]=dict(frames=int(n),pcm_samples=int(samples),audio_ms=int(audio_ms),feature_max_absolute_error=float(error),feature_tolerance=.01)
    for n,error,rmse,rms,snr,clipped in re.findall(r"PCM frames=(\d+) max_error=([\d.]+) rmse=([\d.]+) reference_rms=([\d.]+) snr_db=([-\d.]+) clipped=(\d+)",text):
        details["sequences"][n]["pcm"]=dict(max_absolute_error=float(error),rmse=float(rmse),reference_rms=float(rms),snr_db=float(snr),clipped_samples=int(clipped))
    for kind,name,sample,ticks,cycles,invocations,overflow in re.findall(r"(Method|Sequence|FullFrame)Sample (\w+) (\d+) ticks=(\d+) cycles=(\d+) invocations=(\d+) overflow=(\d+)",text):
        bucket={"Method":"methods","Sequence":"sequences","FullFrame":"full_frames"}[kind]
        record=details[bucket].setdefault(name,{})
        assert int(sample)==len(record.setdefault("cycles_samples",[]))+(kind!="FullFrame")
        record["cycles_samples"].append(int(cycles));record.setdefault("forward_ticks_samples",[]).append(int(ticks))
        record["invocations"]=int(invocations)
        assert int(overflow)==0
    assert set(details["methods"])==set(SHAPES) and set(details["sequences"])=={"2","19","81"}
    platform=dict(os="RTX5",cpu="Cortex-M55",memory={"weights":dict(type="OSPI1",clock_mhz=25,clock_source="RTE/BSP/AE822FA0E5597LS0_M55_HP/ospi_xip_user.h OSPI_CLOCK_MHZ"),"activations":dict(type="SRAM0",clock_mhz=axi/1e6)})
    for kind in ("methods","sequences","full_frames"):
        for name,record in details[kind].items():
            cycles,ticks=record["cycles_samples"],record["forward_ticks_samples"]
            assert len(cycles)==len(ticks)==(10 if kind=="methods" else 3 if kind=="sequences" else int(name)+2)
            record["npu_ms"]=stats.median(cycles)*1000/axi
            record["forward_ms"]=stats.median(ticks)*1000/timer
            record["forward_min_ms"]=min(ticks)*1000/timer;record["forward_max_ms"]=max(ticks)*1000/timer
            run=dict(recorded_at=args.recorded_at,context="rvtts_fargan.Release+DevKit-E8",target="DevKit-E8@RvTTS_FARGAN_Release",device="AE822FA0E5597LS0",core="M55_HP",npu="Ethos-U85",macs=256,compiler="Arm Compiler 6.24.0",optimize="-O3 -ffast-math",build_job=args.build_job,execution_job=args.execution_job,cpu_clock_mhz=cpu/1e6,npu_clock_mhz=axi/1e6,warmup=1,samples=len(ticks),pmu_cross_check="CYCLE == NPU_ACTIVE for every invocation; sums for schedules",pmu_overflow=0,npu_invocations_per_forward=record["invocations"],test_result="FAIL",uart="COM5 115200 8N1 via CMSIS Developer Assistant",model_sha256=details["model_sha256"],firmware_sha256=details["firmware_sha256"],uart_run="benchmark_uart.txt",measurement_source="CMSIS load_and_run "+args.execution_job+"; firmware PMU and extended RTOS system timer")
            if kind=="methods":
                shape,outshape=SHAPES[name]
                input_bytes=4
                for dimension in shape: input_bytes*=dimension
                if name.startswith("fargan_"): input_bytes+=44*8+(964*4 if name=="fargan_init" else 0)
                operation="rvTTS FARGAN "+name+"; all six methods remain loaded"
                weights,arena=record["weights_region_bytes"],record["arena_region_bytes"]
                run.update(test_result="PASS" if record["max_absolute_error"]<=record["tolerance"] else "FAIL",max_absolute_error=record["max_absolute_error"],tolerance=record["tolerance"],input="model_test_data.h held-out quantized-host fixture; input_shape is first float tensor; vocoder methods also take int64[1,44] pitch indices, and init takes zero float[1,964].",timing_scope="ExecuTorch execute, including CPU boundary operators and one Ethos delegate; excludes caller copies, validation, pitch lookup and output DSP.")
                filename="benchmark_"+name+".yml"
            elif kind=="sequences":
                n=int(name);shape,outshape=[n,3],[record["pcm_samples"]];input_bytes=n*4
                operation=f"rvTTS FARGAN full prosody/acoustic/vocoder synthesis, {n} frames, PCM16 output"
                weights=sum(x["weights_region_bytes"] for x in details["methods"].values());arena=max(x["arena_region_bytes"] for x in details["methods"].values())
                record["real_time_factor"]=record["forward_ms"]/record["audio_ms"]
                run.update(max_absolute_error=record["pcm"]["max_absolute_error"],input=f"benchmark_inputs.h seed {dict([(2,800),(19,801),(81,802)])[n]}; logical phone/stress/word columns stored uint8/uint8/uint16; reference float source-model PCM.",timing_scope="OSPI feature/embedding construction, paired bidirectional GRU, acoustic halos, pitch normalization, FARGAN init, N+2 recurrent frames, state feedback/copies, de-emphasis, 80-sample delay removal and final flush, clipped/rounded PCM16. Reference checks and printing excluded. Excludes text frontend, UART, LCD and I2S. FAIL retains both vocoder method numerical failures; waveform error is not a listening-quality acceptance test.")
                filename="benchmark.yml" if name=="81" else "benchmark_"+name+".yml"
            else:
                shape,outshape=[1,996],[160];input_bytes=996*4+44*8
                operation=f"rvTTS FARGAN complete 10ms frame, recurrent feedback in {name}-frame synthesis"
                weights,arena=details["methods"]["fargan_frame"]["weights_region_bytes"],details["methods"]["fargan_frame"]["arena_region_bytes"]
                record["real_time_factor"]=record["forward_ms"]/10
                run.update(input="First timed complete schedule after one warmup; N+2 consecutive frame calls including flush. input_shape is float feature/state tensor; also int64[1,44] indices.",timing_scope="Pitch/embedding input lookup, previous-period indices, neural frame execute, persistent state copies, de-emphasis and PCM16 clipping/rounding. Excludes one-time init, utterance pitch statistics, shared features, UART, LCD and I2S. FAIL retains independent frame-fixture numerical failure.")
                filename="benchmark_full_frame_"+name+".yml"
            network=dict(operation=operation,input_shape=shape,output_shape=outshape,float_boundary=True,quantization="symmetric int16 activations; supplied per-tensor int8 shared weights and per-channel int8 vocoder weights")
            if kind=="sequences":network.update(samples_per_forward=record["pcm_samples"],sample_rate_hz=16000,block_deadline_ms=record["audio_ms"])
            elif kind=="full_frames" or name=="fargan_frame":network.update(samples_per_forward=160,sample_rate_hz=16000,block_deadline_ms=10)
            memory=dict(model_bytes=model_bytes,input_tensor_bytes=input_bytes,ethos=dict(weights_region_bytes=weights,arena_region_bytes=arena),executorch=dict(method_pool_peak_bytes=method_peak,temp_pool_peak_bytes=temp_peak))
            time=dict(ms=record["npu_ms"],cycles=stats.median(cycles),statistic="median",min_cycles=min(cycles),max_cycles=max(cycles),mean_cycles=stats.mean(cycles),forward_ms=record["forward_ms"],forward_timer_hz=timer,cycles_samples=cycles,forward_ticks_samples=ticks)
            document={"benchmarks":dict(network=network,benchmark=[dict(platform=platform,measurements=dict(memory=memory,time=time),run=run)])}
            (HERE/filename).write_text("# Measured experimental FARGAN; strict numerical FAIL preserved.\n"+yaml.safe_dump(document,sort_keys=False),encoding="utf-8")
    (HERE/"benchmark_details.json").write_text(json.dumps(details,indent=2)+"\n",encoding="utf-8")

if __name__=="__main__": main()
