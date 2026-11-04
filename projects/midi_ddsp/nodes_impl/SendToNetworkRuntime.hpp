// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "../player/midi_file.hpp"
#include "../player/score_windows.hpp"
#include "../assets/demo_midi.h"
#include "../stream_types.h"
#include "../hardware_params.h"
#include "../stream_init.hpp"
#include "../audio_output.hpp"
#include <atomic>
extern "C" {
volatile uint32_t midi_audio_nonfinite,midi_audio_clipped,midi_audio_blocks;
volatile uint32_t midi_event_dropped,midi_network_requests,midi_network_completions;
struct MidiDemoStats {
 uint32_t windows_started,windows_ready,windows_played,frames,tail_frames,complete,audio_complete;
 uint32_t current_instrument,instrument_mask,tail_steals,control_waits,deadline_misses;
 uint32_t max_regions,method_calls[8];
 uint64_t neural_ticks,max_neural_ticks,control_wait_ticks,startup_ticks;
 int32_t error;
};
MidiDemoStats midi_demo;
midi_ddsp::ScoreWindowCounters midi_admission;
}
namespace {
using midi_ddsp::ScoreWindow;
// Private static backend context of the project CPU schedule node. No heap or
// duplicate audio/control bank is required. The graph callbacks receive this
// opaque context; all score and handoff state remains private to this backend.
class MidiScheduleRuntime {
private:
midi_ddsp::MidiEvent demo_events[1024];
midi_ddsp::ScoreWindows score_reader;
ScoreWindow score_window;
float next_pitch[1000],current_pitch[1000];
uint8_t current_active[1000];
float current_gain[1000];
std::atomic<bool> neural_busy{false},current_ready{false},pending_ready{false},score_finished{false};
const float *pending_output;
unsigned nn_method,nn_call,nn_regions,random_state,frame_cursor=0,current_id=0,render_id=0;
bool first_render=true,reset_render=true;
uint64_t neural_start;
uint64_t demo_start_clock;
osEventFlagsId_t demo_events_flags;
enum { DemoReady=1,DemoPending=2,DemoRelease=4 };

int demo_fail(int e){midi_demo.error=e;pipeline_error=e;if(demo_events_flags)osEventFlagsSet(demo_events_flags,DemoReady|DemoPending|DemoRelease);return e;}
unsigned region_pitch(unsigned n){return score_window.regions[n].pitch;}
void prepare_expression_input(unsigned method,unsigned call){
 const unsigned n=method==1?nn_regions-1-call:call;
 if(method<2){std::memcpy(packed,note_emb[n],192*4);std::memcpy(packed+192,state1,128*4);}
 else {std::memcpy(packed,note_context[n],256*4);std::memcpy(packed+256,previous,6*4);std::memcpy(packed+262,state1,128*4);std::memcpy(packed+390,state2,128*4);}
}
void prepare_precondition(unsigned base){
 for(unsigned row=0;row<16;++row){float *features=packed+row*10;const unsigned f=base+row;
  if(f>=1000){std::memset(features,0,10*4);continue;}
  const unsigned n=score_window.frame_region[f];
  for(unsigned c=0;c<6;++c)features[c]=std::min(1.f,std::max(0.f,expression[n][c]));
  midi_ddsp::score_frame_features(score_window,f,features+6);
 }
}
void prepare_context(unsigned method,unsigned call){
 unsigned f=method==5?999-call:call;
 std::memcpy(packed,phase.sequence.z[f],320*4);std::memcpy(packed+320,state1,256*4);
}
void prepare_pitch(unsigned f){
 std::memcpy(packed,phase.sequence.context[f],512*4);std::memcpy(packed+512,previous,201*4);
 std::memcpy(packed+713,state1,256*4);std::memcpy(packed+969,state2,256*4);
}
int demo_next_window(void*){
 if(neural_busy||score_finished)return 0;
 if(!score_reader.next(score_window)){midi_admission=score_reader.counters();score_finished=true;osEventFlagsSet(demo_events_flags,DemoPending);return 0;}
 midi_admission=score_reader.counters();
 // A rest-only window keeps the selected instrument and its exact IR.
 if(score_window.instrument<0)score_window.instrument=int8_t(current_id);
 neural_busy=true;nn_regions=score_window.region_count;nn_call=0;nn_method=0;
 midi_demo.max_regions=std::max(midi_demo.max_regions,nn_regions);
 ++midi_demo.windows_started;neural_start=clock64();return 1;
}
int demo_prepare(void*,int completed,const float *out){
 if(completed<0){
  if(!nn_regions||nn_regions>16)return demo_fail(-801);
  const unsigned id=unsigned(score_window.instrument);
  for(unsigned n=0;n<nn_regions;++n)for(unsigned c=0;c<64;++c){
   note_emb[n][c]=asset(0)[region_pitch(n)*64+c];
   note_emb[n][64+c]=(score_window.regions[n].length*.004f)*asset(36096)[c]+asset(36352)[c];
   note_emb[n][128+c]=asset(32768)[id*64+c];
  }
  std::memset(state1,0,sizeof(state1));prepare_expression_input(0,0);return 0;
 }
 if(!out)return demo_fail(-802);
 ++midi_demo.method_calls[completed];
 const unsigned id=unsigned(score_window.instrument);
 if(completed<2){
  const unsigned n=completed==1?nn_regions-1-nn_call:nn_call;
  std::memcpy(state1,out,128*4);std::memcpy(note_context[n]+128*completed,out,128*4);
  if(++nn_call<nn_regions){prepare_expression_input(completed,nn_call);return completed;}
  nn_call=0;nn_method=unsigned(completed)+1;std::memset(state1,0,sizeof(state1));
  if(completed==1){std::memset(state2,0,sizeof(state2));std::memset(previous,0,sizeof(previous));}
  prepare_expression_input(nn_method,0);return nn_method;
 }
 if(completed==2){
  for(unsigned c=0;c<6;++c)expression[nn_call][c]=previous[c]=region_pitch(nn_call)?out[c]:0;
  std::memcpy(state1,out+6,128*4);std::memcpy(state2,out+134,128*4);
  if(++nn_call<nn_regions){prepare_expression_input(2,nn_call);return 2;}
  nn_call=0;prepare_precondition(0);return 3;
 }
 if(completed==3){
  const unsigned valid=std::min(16U,1000-nn_call);
  for(unsigned row=0;row<valid;++row){std::memcpy(phase.sequence.z[nn_call+row],out+row*256,256*4);std::memcpy(phase.sequence.z[nn_call+row]+256,asset(36608)+id*64,64*4);}
  nn_call+=valid;if(nn_call<1000){prepare_precondition(nn_call);return 3;}
  nn_call=0;std::memset(state1,0,sizeof(state1));prepare_context(4,0);return 4;
 }
 if(completed==4||completed==5){
  const unsigned f=completed==5?999-nn_call:nn_call;
  std::memcpy(state1,out,256*4);std::memcpy(phase.sequence.context[f]+256*(completed-4),out,256*4);
  if(++nn_call<1000){prepare_context(completed,nn_call);return completed;}
  nn_call=0;std::memset(state1,0,sizeof(state1));
  if(completed==4){prepare_context(5,0);return 5;}
  midi_stage_release();module=nullptr;
  for(unsigned f=0;f<1000;++f)std::memcpy(input+384*f,phase.sequence.z[f],320*4);
  std::memset(state2,0,sizeof(state2));std::memset(previous,0,sizeof(previous));random_state=9100+id;
  prepare_pitch(0);return 6;
 }
 if(completed==6){
  std::memcpy(state1,out+201,256*4);std::memcpy(state2,out+457,256*4);
  random_state=1664525U*random_state+1013904223U;volatile float u=float(random_state>>8)*0x1p-24f;volatile float scaled=u*.98f;
  const unsigned index=sample_pitch(out,scaled+.01f);std::memset(previous,0,sizeof(previous));previous[index]=1;
  float features[4];midi_ddsp::score_frame_features(score_window,nn_call,features);
  volatile float base=features[0]*127.f;volatile float offset=float(index)*.01f;next_pitch[nn_call]=base+offset-1;
  if(++nn_call<1000){prepare_pitch(nn_call);return 6;}
  for(unsigned f=0;f<1000;++f)for(unsigned c=0;c<64;++c)input[384*f+320+c]=(next_pitch[f]/127.f)*asset(39936)[c]+asset(40192)[c];
  return 7;
 }
 if(completed!=7)return demo_fail(-803);
 const uint64_t elapsed=clock64()-neural_start;midi_demo.neural_ticks+=elapsed;midi_demo.max_neural_ticks=std::max(midi_demo.max_neural_ticks,elapsed);
 if(midi_demo.windows_started>1&&elapsed>uint64_t(midi_timer_hz)*4)++midi_demo.deadline_misses;
 pending_output=out;__DMB();pending_ready=true;osEventFlagsSet(demo_events_flags,DemoPending);
 while(current_ready)osEventFlagsWait(demo_events_flags,DemoRelease,osFlagsWaitAny,osWaitForever);
 const bool changed=!first_render&&id!=current_id;
 if(changed)++midi_demo.tail_steals; // Measured cap1: explicitly steal the old exact tail.
 current_id=id;reset_render=first_render||changed;
 std::memcpy(controls,out,sizeof(controls));
 for(unsigned f=0;f<1000;++f){const unsigned note=score_window.regions[score_window.frame_region[f]].pitch;
  const auto &cc=score_window.controls[f];const float bend=(int(cc.pitch_bend)-8192)*(2.f/8192);
  const float p=next_pitch[f]+bend;current_pitch[f]=note?440.f*std::exp2((p-69)/12):0;
  current_active[f]=note&&f<score_window.valid_frames&&f<score_window.capacity_rest_start;
  current_gain[f]=(cc.velocity/127.f)*(cc.volume/127.f)*(cc.expression/127.f);
 }
 midi_demo.current_instrument=id;midi_demo.instrument_mask|=1U<<id;++midi_demo.windows_ready;
 midi_stage_release();module=nullptr;first_render=false;
 __DMB();pending_ready=false;current_ready=true;neural_busy=false;
 osEventFlagsSet(demo_events_flags,DemoReady);return -1;
}
const float*demo_invoke(void*,unsigned method){
 // This callback is called exclusively by the corresponding PTE method node.
 const float*out=invoke(method);if(!out)demo_fail(schedule_error);return out;
}
void wait_ready(){while(!current_ready&&!midi_demo.error){++midi_demo.control_waits;const uint64_t t=clock64();osEventFlagsWait(demo_events_flags,DemoReady,osFlagsWaitAny,osWaitForever);midi_demo.control_wait_ticks+=clock64()-t;}}
int demo_control_pair(void*,float *pair){
 std::memset(pair,0,256*4);
 if(!current_ready&&score_finished&&!neural_busy){++midi_demo.tail_frames;if(midi_demo.tail_frames>=750)midi_demo.complete=1;return 0;}
 wait_ready();if(midi_demo.error)return midi_demo.error;if(!midi_demo.frames)midi_demo.startup_ticks=clock64()-demo_start_clock;const unsigned f=frame_cursor;render_id=current_id;
 std::memcpy(pair,controls[f],126*4);pair[126]=current_pitch[f];unsigned flags=MIDI_SYNTH_ENABLED|(current_active[f]?MIDI_DRY_ACTIVE:0);
 if(reset_render){flags|=MIDI_RESET_SYNTH;reset_render=false;}
 pair[255]=current_gain[f];
 if(f<999){std::memcpy(pair+127,controls[f+1],126*4);pair[253]=current_pitch[f+1];}
 else {
  while(!pending_ready&&!score_finished&&!midi_demo.error){++midi_demo.control_waits;osEventFlagsWait(demo_events_flags,DemoPending,osFlagsWaitAny,osWaitForever);}
  if(midi_demo.error)return midi_demo.error;
  if(pending_ready&&unsigned(score_window.instrument)==current_id){std::memcpy(pair+127,pending_output,126*4);const unsigned n=score_window.regions[score_window.frame_region[0]].pitch;
   const float bend=(int(score_window.controls[0].pitch_bend)-8192)*(2.f/8192);
   pair[253]=n?440.f*std::exp2((next_pitch[0]+bend-69)/12):0;}
  else {std::memcpy(pair+127,pair,126*4);pair[253]=pair[126];flags|=MIDI_FINITE_ENDPOINT;}
  current_ready=false;frame_cursor=0;++midi_demo.windows_played;osEventFlagsSet(demo_events_flags,DemoRelease);
 }
 pair[254]=float(flags);if(f<999)++frame_cursor;++midi_demo.frames;return 0;
}
const float *demo_impulse(void*){return asset(MIDI_ASSET_S_REVERB_MODULE_MAGNITUDES_EMBEDDING_EMBEDDINGS_OFFSET)+render_id*48000;}
int demo_finished(void*){return midi_demo.complete;}
static int demo_audio_finish(){const int e=midi_ddsp_audio_drain(1000);midi_ddsp_audio_stop();if(!e)midi_demo.audio_complete=1;return e;}
public:
int start_demo(){
 demo_start_clock=clock64();
 const auto score=midi_ddsp::parse_midi_file(midi_demo_file,sizeof(midi_demo_file),demo_events,1024);
 if(score.error!=midi_ddsp::MidiError::none)return -810;
 if(!score_reader.configure(demo_events,score.event_count,score.duration_us,16))return -811;
 demo_events_flags=osEventFlagsNew(nullptr);if(!demo_events_flags)return -812;
 schedule_report=&midi_schedule[0];schedule_error=0;
 HardwareParams hw{};int error=hardware_params_init(&hw);if(error)return error;
 hw.network=this;hw.invoke=invoke_bridge;hw.next_window=next_bridge;hw.prepare=prepare_bridge;hw.controls=controls_bridge;hw.impulse=impulse_bridge;
 hw.finished=finished_bridge;hw.audio_finish=demo_audio_finish;
 return stream_configure_and_start(&hw);
}
private:
 static MidiScheduleRuntime&self(void*p){return *static_cast<MidiScheduleRuntime*>(p);}
 static const float*invoke_bridge(void*p,unsigned m){return self(p).demo_invoke(p,m);}
 static int next_bridge(void*p){return self(p).demo_next_window(p);}
 static int prepare_bridge(void*p,int m,const float*out){return self(p).demo_prepare(p,m,out);}
 static int controls_bridge(void*p,float*out){return self(p).demo_control_pair(p,out);}
 static const float*impulse_bridge(void*p){return self(p).demo_impulse(p);}
 static int finished_bridge(void*p){return self(p).demo_finished(p);}
};
// One admitted instrument stream and one graph instance own this fixed state.
MidiScheduleRuntime midi_schedule_runtime;
int start_demo(){return midi_schedule_runtime.start_demo();}
}
