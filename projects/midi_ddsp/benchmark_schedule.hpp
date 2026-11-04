// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "model/schedule_fixture.h"
extern "C" {
struct MidiScheduleReport {
  MidiTiming method[8];
  uint64_t total_ticks,cpu_ticks;
  MidiStageStats staging[8];
  MidiQuantizationStats quantization[8];
  MidiNormalizationStats normalization[8];
  MidiCacheStats cache[8];
  uint32_t routes[4];
  uint32_t calls[8],frames_done,complete,finite_outputs;
  uint32_t precondition_batch,precondition_valid_frames,precondition_padding_frames;
  float trace_max[8],expression[24],pitch[1000];
  float pitch_mean[4],pitch_std[4],raw_mean[126],raw_rms[126];
  float pitch_mae,raw_selected_max,uniform_first_eight[8];
};
MidiScheduleReport midi_schedule[MIDI_SCHEDULE_SWEEP?15:1];
volatile uint32_t midi_schedule_instrument,midi_schedule_stage;
volatile uint32_t midi_schedule_runtime_error;
}
namespace {
// Persistent until reset; outputs from a method remain valid only until that
// same method executes again. Copy every recurrent state and retained context.
float state1[256],state2[256],previous[201];
float note_emb[64][192] __attribute__((section(".bss.ai_pool")));
float note_context[64][256] __attribute__((section(".bss.ai_pool")));
float expression[64][6] __attribute__((section(".bss.ai_pool")));
MidiScheduleReport *schedule_report;
int schedule_error;
const float *invoke(unsigned index) {
  using namespace executorch::aten;
  auto &f=midi_fixtures[index];
  if(!prepare_stage(index)){schedule_error=-601-int(index);return nullptr;}
  std::array<SizesType,3> sizes{f.shape[0],f.shape[1],f.shape[2]};
  std::array<DimOrderType,3> order{0,1,2};
  TensorImpl impl(ScalarType::Float,f.rank,sizes.data(),index==7?input:packed,order.data());
  midi_quantization_current_method=index;
  ethos_benchmark_reset();const uint64_t start=clock64();
  auto result=module->execute(f.name,Tensor(&impl));const uint64_t elapsed=clock64()-start;
  auto &r=*schedule_report;auto &t=r.method[index];
  t.ticks+=elapsed;t.invocations+=ethos_benchmark.invocations;t.overflow|=ethos_benchmark.overflow;
  t.cycles+=ethos_benchmark.cycles;t.active+=ethos_benchmark.active_cycles;
  ++r.calls[index];
  if(!result.ok()){midi_schedule_runtime_error=uint32_t(result.error());schedule_error=-500-int(index);return nullptr;}
  if(result->size()!=1||!result->front().isTensor()){schedule_error=-540-int(index);return nullptr;}
  const auto out=result->front().toTensor();
  if(out.numel()!=f.output_count||out.scalar_type()!=ScalarType::Float){schedule_error=-510-int(index);return nullptr;}
  const float *data=out.const_data_ptr<float>();
  for(unsigned i=0;i<f.output_count;++i)if(!finite(data[i])){schedule_error=-520-int(index);return nullptr;}
  if(t.overflow||!ethos_benchmark.invocations||ethos_benchmark.cycles!=ethos_benchmark.active_cycles){schedule_error=-530-int(index);return nullptr;}
  return data;
}
void trace(unsigned method,unsigned call,const float *out) {
  if(midi_schedule_instrument!=0)return;
  const float *expected[]={midi_schedule_expected_expression_forward,midi_schedule_expected_expression_backward,
    midi_schedule_expected_expression_step,midi_schedule_expected_precondition,midi_schedule_expected_context_forward,
    midi_schedule_expected_context_backward,midi_schedule_expected_pitch_step};
  unsigned selected=call;
  if(method>=3){selected=8;for(unsigned i=0;i<8;++i)if(midi_schedule_selected_frames[i]==call)selected=i;if(selected==8)return;}
  const unsigned width=method==3?256:midi_fixtures[method].output_count;
  for(unsigned i=0;i<width;++i)schedule_report->trace_max[method]=std::max(schedule_report->trace_max[method],std::fabs(out[i]-expected[method][selected*width+i]));
}
unsigned sample_pitch(const float *logits,float uniform) {
  float ordered[201],probability[201];std::memcpy(ordered,logits,sizeof(ordered));
  std::sort(ordered,ordered+201,[](float a,float b){return a>b;});
  float total=0;for(unsigned i=0;i<201;++i){probability[i]=std::exp(ordered[i]-ordered[0]);total+=probability[i];}
  float cumulative=0,threshold=1000.f;
  for(unsigned i=0;i<201;++i){const float p=probability[i]/total;cumulative+=p;if(cumulative-p<.95f)threshold=std::min(threshold,ordered[i]);}
  total=0;for(unsigned i=0;i<201;++i){probability[i]=logits[i]<threshold?0.f:std::exp(logits[i]-ordered[0]);total+=probability[i];}
  cumulative=0;unsigned index=0;for(unsigned i=0;i<201;++i){cumulative+=probability[i]/total;index+=cumulative<uniform;}
  return std::min(index,200U);
}
const float *asset(unsigned byte_offset){return reinterpret_cast<const float*>(MODEL_ASSET_CPU_WEIGHTS_ADDRESS+byte_offset);}
int run_schedule(unsigned instrument,unsigned report_index) {
  if(instrument>=13)return -590;
  if(report_index==~0U)report_index=instrument;
  if(report_index>=15)return -591;
  auto &r=midi_schedule[report_index];schedule_report=&r;schedule_error=0;
  MidiStageStats staging_before[8];std::memcpy(staging_before,midi_stage_stats,sizeof(staging_before));
  MidiQuantizationStats quantization_before[8];std::memcpy(quantization_before,midi_quantization_stats,sizeof(quantization_before));
  MidiNormalizationStats normalization_before[8];std::memcpy(normalization_before,midi_normalization_stats,sizeof(normalization_before));
  MidiCacheStats cache_before[8];std::memcpy(cache_before,midi_cache_stats,sizeof(cache_before));
  uint32_t routes_before[4];std::memcpy(routes_before,midi_stage_routes,sizeof(routes_before));
  midi_schedule_instrument=instrument;midi_schedule_stage=1;
  const uint64_t start=clock64();
  const unsigned notes[]={48+instrument,55+instrument,0,60+instrument};
  // Learned pitch embedding, duration affine (one second), instrument embedding.
  for(unsigned n=0;n<4;++n)for(unsigned c=0;c<64;++c){note_emb[n][c]=asset(0)[notes[n]*64+c];note_emb[n][64+c]=asset(36096)[c]+asset(36352)[c];note_emb[n][128+c]=asset(32768)[instrument*64+c];}
  for(unsigned direction=0;direction<2;++direction){std::memset(state1,0,sizeof(state1));
    for(unsigned call=0;call<4;++call){const unsigned n=direction?3-call:call;
      std::memcpy(packed,note_emb[n],192*4);std::memcpy(packed+192,state1,128*4);
      auto out=invoke(direction);if(!out)return schedule_error;trace(direction,call,out);
      std::memcpy(state1,out,128*4);std::memcpy(note_context[n]+128*direction,out,128*4);
    }
  }
  midi_schedule_stage=2;std::memset(state1,0,sizeof(state1));std::memset(state2,0,sizeof(state2));std::memset(previous,0,sizeof(previous));
  for(unsigned n=0;n<4;++n){std::memcpy(packed,note_context[n],256*4);std::memcpy(packed+256,previous,6*4);std::memcpy(packed+262,state1,128*4);std::memcpy(packed+390,state2,128*4);
    auto out=invoke(2);if(!out)return schedule_error;trace(2,n,out);
    for(unsigned c=0;c<6;++c)r.expression[n*6+c]=expression[n][c]=previous[c]=notes[n]?out[c]:0.f;
    std::memcpy(state1,out+6,128*4);std::memcpy(state2,out+134,128*4);
  }
  midi_schedule_stage=3;
  const unsigned batch=midi_fixtures[3].input_count/10;
  if(!batch||batch*10>sizeof(packed)/sizeof(packed[0])||midi_fixtures[3].input_count!=batch*10||midi_fixtures[3].output_count!=batch*256)return -592;
  r.precondition_batch=batch;
  for(unsigned base=0;base<1000;base+=batch){const unsigned valid=std::min(batch,1000-base);
    for(unsigned row=0;row<batch;++row){float *features=packed+row*10;
      if(row>=valid){std::memset(features,0,10*4);continue;}
      const unsigned f=base+row,n=f/250;
      for(unsigned c=0;c<6;++c)features[c]=std::min(1.f,std::max(0.f,expression[n][c]));
      features[6]=notes[n]/127.f;features[7]=f%250==0?1.f:0.f;features[8]=f%250==249?1.f:0.f;features[9]=notes[n]?(f%250+1)/250.f:0.f;
    }
    auto out=invoke(3);if(!out)return schedule_error;
    for(unsigned row=0;row<valid;++row){const unsigned f=base+row;trace(3,f,out+row*256);std::memcpy(phase.sequence.z[f],out+row*256,256*4);
      std::memcpy(phase.sequence.z[f]+256,asset(36608)+instrument*64,64*4);}
    r.precondition_valid_frames+=valid;r.precondition_padding_frames+=batch-valid;
  }
  for(unsigned direction=0;direction<2;++direction){midi_schedule_stage=4+direction;std::memset(state1,0,sizeof(state1));
    for(unsigned call=0;call<1000;++call){const unsigned f=direction?999-call:call;
      std::memcpy(packed,phase.sequence.z[f],320*4);std::memcpy(packed+320,state1,256*4);
      auto out=invoke(4+direction);if(!out)return schedule_error;trace(4+direction,call,out);
      std::memcpy(state1,out,256*4);std::memcpy(phase.sequence.context[f]+256*direction,out,256*4);
    }
  }
  midi_schedule_stage=6;
#if MIDI_STAGE_DELEGATES
  // The prior context method's staged weights occupy input. Destroy that
  // method before replacing them with retained z. This frees phase.z for
  // the larger pitch-stage blob while all512 context features stay live.
  midi_stage_release();module=nullptr;
  for(unsigned f=0;f<1000;++f)std::memcpy(input+384*f,phase.sequence.z[f],320*4);
#endif
  std::memset(state1,0,sizeof(state1));std::memset(state2,0,sizeof(state2));std::memset(previous,0,sizeof(previous));
  uint32_t random=9100+instrument;
  for(unsigned f=0;f<1000;++f){std::memcpy(packed,phase.sequence.context[f],512*4);std::memcpy(packed+512,previous,201*4);std::memcpy(packed+713,state1,256*4);std::memcpy(packed+969,state2,256*4);
    auto out=invoke(6);if(!out)return schedule_error;trace(6,f,out);
    std::memcpy(state1,out+201,256*4);std::memcpy(state2,out+457,256*4);
    random=1664525U*random+1013904223U;volatile float u=float(random>>8)*0x1p-24f;volatile float scaled=u*.98f;const float uniform=scaled+.01f;
    if(f<8)r.uniform_first_eight[f]=uniform;
    const unsigned index=sample_pitch(out,uniform);std::memset(previous,0,sizeof(previous));previous[index]=1.f;
    // Reproduce score-feature multiply by 127, then the original pitch offset.
    volatile float base=(notes[f/250]/127.f)*127.f;volatile float offset=float(index)*.01f;
    r.pitch[f]=base+offset-1.f;r.frames_done=f+1;
  }
  midi_schedule_stage=7;
  for(unsigned f=0;f<1000;++f){
#if !MIDI_STAGE_DELEGATES
    std::memcpy(input+384*f,phase.sequence.z[f],320*4);
#endif
    for(unsigned c=0;c<64;++c)input[384*f+320+c]=(r.pitch[f]/127.f)*asset(39936)[c]+asset(40192)[c];}
  // z and bidirectional context are now dead. Harmonic planned memory may
  // overwrite the union without affecting the separate full-block input.
  midi_schedule_stage=8;auto out=invoke(7);if(!out)return schedule_error;std::memcpy(controls,out,sizeof(controls));
  if(instrument==0)for(unsigned selected=0;selected<8;++selected)for(unsigned c=0;c<126;++c)r.raw_selected_max=std::max(r.raw_selected_max,std::fabs(controls[midi_schedule_selected_frames[selected]][c]-midi_schedule_expected_raw_selected[selected*126+c]));
  for(unsigned n=0;n<4;++n){double sum=0,square=0;for(unsigned f=n*250;f<(n+1)*250;++f){sum+=r.pitch[f];square+=double(r.pitch[f])*r.pitch[f];if(instrument==0)r.pitch_mae+=std::fabs(r.pitch[f]-midi_schedule_expected_pitch[f])/1000.f;}const double mean=sum/250;r.pitch_mean[n]=mean;r.pitch_std[n]=std::sqrt(std::max(0.,square/250-mean*mean));}
  for(unsigned c=0;c<126;++c){double sum=0,square=0;for(unsigned f=0;f<1000;++f){sum+=controls[f][c];square+=double(controls[f][c])*controls[f][c];}r.raw_mean[c]=sum/1000;r.raw_rms[c]=std::sqrt(square/1000);}
#if MIDI_STAGE_DELEGATES
  midi_stage_release();module=nullptr;
#endif
  r.total_ticks=clock64()-start;uint64_t method_ticks=0;for(auto &t:r.method)method_ticks+=t.ticks;r.cpu_ticks=r.total_ticks-method_ticks;
  for(unsigned i=0;i<8;++i){const auto &a=midi_stage_stats[i],&b=staging_before[i];auto &s=r.staging[i];s=a;
    s.prepare_ticks-=b.prepare_ticks;s.copy_ticks-=b.copy_ticks;s.memcpy_ticks-=b.memcpy_ticks;s.cache_clean_ticks-=b.cache_clean_ticks;s.crc_ticks-=b.crc_ticks;
    s.backend_bytes-=b.backend_bytes;s.constant_bytes-=b.constant_bytes;s.constant_bypasses-=b.constant_bypasses;s.loads-=b.loads;s.crc_failures-=b.crc_failures;
    auto &q=r.quantization[i];q=midi_quantization_stats[i];const auto &p=quantization_before[i];
    for(unsigned j=0;j<2;++j){q.ticks[j]-=p.ticks[j];q.calls[j]-=p.calls[j];q.elements[j]-=p.elements[j];q.nonzero_points[j]-=p.nonzero_points[j];q.fast[j]-=p.fast[j];q.fallback[j]-=p.fallback[j];}}
  for(unsigned i=0;i<8;++i){auto &s=r.cache[i];s=midi_cache_stats[i];for(unsigned j=0;j<2;++j){s.ticks[j]-=cache_before[i].ticks[j];s.calls[j]-=cache_before[i].calls[j];}}
  for(unsigned i=0;i<8;++i){auto &s=r.normalization[i];s=midi_normalization_stats[i];for(unsigned j=0;j<6;++j){s.ticks[j]-=normalization_before[i].ticks[j];s.calls[j]-=normalization_before[i].calls[j];}}
  for(unsigned i=0;i<4;++i)r.routes[i]=midi_stage_routes[i]-routes_before[i];
  r.finite_outputs=1;r.complete=1;midi_schedule_stage=9;return 0;
}
}
