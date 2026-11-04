// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "../../../common/nodes_impl/c/NodeSupport.hpp"
#include "c/ddsp_synth.hpp"
#include "stream_types.h"
template<typename IN,int n,typename OUT,int no>class DDSPSynth:public GenericNode<IN,n,OUT,no>{
public:
 DDSPSynth(FIFOBase<IN>&i,FIFOBase<OUT>&o):GenericNode<IN,n,OUT,no>(i,o){}
 int run()final {
  static_assert(n==256&&no==64,"MIDI-DDSP control/audio shape");
  const float *pair=this->getReadBuffer();float *dry=this->getWriteBuffer();
  const unsigned flags=unsigned(pair[254]);if(!(flags&MIDI_SYNTH_ENABLED)){std::memset(dry,0,64*4);return CG_SUCCESS;}
  float white[64],harmonic[64],noise[64];
  if(!primed_||(flags&MIDI_RESET_SYNTH)){synth_.reset();random_=19100;fill(white);synth_.prime(pair,white);primed_=true;}
  fill(white);const float *next_white=flags&MIDI_FINITE_ENDPOINT?nullptr:white;
  if(!(flags&MIDI_DRY_ACTIVE)&&pair[126]==0&&pair[253]==0){synth_.advance_muted_zero(pair+127,next_white);std::memset(dry,0,64*4);return CG_SUCCESS;}
  synth_.interval(pair,pair+127,pair[126],pair[253],next_white,harmonic,noise,dry);
  for(unsigned j=0;j<64;++j)dry[j]=flags&MIDI_DRY_ACTIVE?dry[j]*pair[255]:0;
  return CG_SUCCESS;
 }
private:
 void fill(float *white){for(unsigned j=0;j<64;++j){random_=1664525U*random_+1013904223U;white[j]=float(random_>>8)*0x1p-23f-1.f;}}
 midi_ddsp::Synth synth_;uint32_t random_=19100;bool primed_=false;
};
