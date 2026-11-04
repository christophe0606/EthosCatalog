// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "nodes_impl/c/ddsp_synth.hpp"
#include "nodes_impl/c/ddsp_reverb256.hpp"
#include "model/cpu_asset_layout.h"
#include "model/dsp_probe.h"
#define MIDI_REVERB_BACKEND_ONLY
#include "nodes_impl/DDSPReverb.hpp"
#undef MIDI_REVERB_BACKEND_ONLY
extern "C" {
struct MidiDspProfile {uint64_t init_ticks,harmonic_noise_ticks,reverb_ticks,total_ticks;uint32_t intervals,nonfinite,clipped,complete,instrument,report_slot;float peak,rms;};
MidiDspProfile midi_dsp_profile;
float midi_dsp_selftest_max[4];
uint32_t midi_dsp_selftest_cases,midi_dsp_selftest_complete;
}
namespace {
float *const reverb_coefficients=midi_reverb_banks().coefficients;
float *const reverb_history=midi_reverb_banks().history;
int dsp_selftest(){
 // Neural methods have finished and released their staged storage. Reuse only
 // the dead input arena for this temporary IR; controls remain untouched.
 std::memset(input,0,48000*4);
 for(unsigned j=0;j<8;++j)input[midi_dsp_probe_ir_indices[j]]=midi_dsp_probe_ir_values[j];
 midi_ddsp::Reverb256 reverb;reverb.init(reverb_coefficients,reverb_history,input,true);
 midi_ddsp::Synth synth;synth.reset();float raw[126],next[126],white[64],output[4][64];
 uint32_t rs=midi_dsp_probe_raw_seed,ws=midi_dsp_probe_white_seed;
 midi_dsp_probe_fill_raw(rs,raw);midi_dsp_probe_fill_white(ws,white);synth.prime(raw,white);
 unsigned selected=0;float grouped_dry[256]{},grouped_wet[256];
 for(unsigned f=0;f<midi_dsp_probe_frames;++f){const bool last=f+1==midi_dsp_probe_frames;
  if(last)std::memcpy(next,raw,sizeof(raw));else {midi_dsp_probe_fill_raw(rs,next);midi_dsp_probe_fill_white(ws,white);}
  synth.interval(raw,next,midi_dsp_probe_hz(f),midi_dsp_probe_hz(last?f:f+1),last?nullptr:white,output[0],output[1],output[2]);
  std::memcpy(grouped_dry+(f%4)*64,output[2],64*4);
  if(f%4==3||last){if(last&&f%4!=3)std::memset(grouped_dry+(f%4+1)*64,0,(3-f%4)*64*4);reverb.process256(grouped_dry,grouped_wet);
   const unsigned base=f-f%4;for(unsigned k=0;k<10;++k){const unsigned interval=midi_dsp_probe_selected[k];if(interval<base||interval>f)continue;
    for(unsigned s=0;s<64;++s){const float actual=grouped_wet[(interval-base)*64+s];if(!finite(actual))return -751;
     midi_dsp_selftest_max[3]=std::max(midi_dsp_selftest_max[3],std::fabs(actual-midi_dsp_probe_expected[k][3][s]));++midi_dsp_selftest_cases;}}
  }
  if(selected<10&&f==midi_dsp_probe_selected[selected]){for(unsigned c=0;c<3;++c)for(unsigned s=0;s<64;++s){
   if(!finite(output[c][s]))return -751;
   midi_dsp_selftest_max[c]=std::max(midi_dsp_selftest_max[c],std::fabs(output[c][s]-midi_dsp_probe_expected[selected][c][s]));++midi_dsp_selftest_cases;}
   ++selected;}
  std::memcpy(raw,next,sizeof(raw));
 }
 for(float error:midi_dsp_selftest_max)if(error>=midi_dsp_probe_tolerance)return -752;
 midi_dsp_selftest_complete=1;return 0;
}
int profile_dsp(){
 constexpr unsigned slot=MIDI_SCHEDULE_SWEEP?14:0;
 midi_dsp_profile.instrument=midi_schedule_instrument;midi_dsp_profile.report_slot=slot;
 midi_ddsp::Synth synth;synth.reset();midi_ddsp::Reverb256 reverb;
 const float *learned=reinterpret_cast<const float*>(MODEL_ASSET_CPU_WEIGHTS_ADDRESS+MIDI_ASSET_S_REVERB_MODULE_MAGNITUDES_EMBEDDING_EMBEDDINGS_OFFSET)+midi_schedule_instrument*48000;
 uint64_t start=clock64();reverb.init(reverb_coefficients,reverb_history,learned,true);midi_dsp_profile.init_ticks=clock64()-start;
 uint32_t random=19100;float white[64],harmonic[64],noise[64],dry[256],wet[256];
 auto fill=[&](){for(float &v:white){random=1664525U*random+1013904223U;v=float(random>>8)*0x1p-23f-1.f;}};
 fill();synth.prime(controls[0],white);double square=0;start=clock64();
 for(unsigned f=0;f<1000;++f){const unsigned next=std::min(f+1,999U);fill();uint64_t tick=clock64();
  float hz=midi_schedule[slot].pitch[f],next_hz=midi_schedule[slot].pitch[next];hz=hz==0?0:440.f*std::exp2((hz-69)/12);next_hz=next_hz==0?0:440.f*std::exp2((next_hz-69)/12);
  synth.interval(controls[f],controls[next],hz,next_hz,f==999?nullptr:white,harmonic,noise,dry+(f%4)*64);midi_dsp_profile.harmonic_noise_ticks+=clock64()-tick;
  if(f%4==3){tick=clock64();reverb.process256(dry,wet);midi_dsp_profile.reverb_ticks+=clock64()-tick;
   for(float v:wet){if(!finite(v))++midi_dsp_profile.nonfinite;if(std::fabs(v)>1)++midi_dsp_profile.clipped;midi_dsp_profile.peak=std::max(midi_dsp_profile.peak,std::fabs(v));square+=double(v)*v;}}
  ++midi_dsp_profile.intervals;
 }
 midi_dsp_profile.total_ticks=clock64()-start;midi_dsp_profile.rms=std::sqrt(square/64000);midi_dsp_profile.complete=1;return midi_dsp_profile.nonfinite?-750:0;
}
}
