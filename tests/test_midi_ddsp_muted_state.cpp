// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "ddsp_synth.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
namespace {
uint32_t random_state=20261104;
float sample(){random_state=random_state*1664525U+1013904223U;return float(int(random_state>>23)-256)/256;}
void fill(float *p,unsigned count){for(unsigned i=0;i<count;++i)p[i]=sample();}
}
int main(){
 float maximum=0;unsigned skipped=0,compared=0;
 for(unsigned rest:{1U,4U,17U,65U,1001U,1065U}){
  // Initial note, alternating short rests/notes, a long muted rest, and a
  // different audible pitch expose phase and filtered-noise state on return.
  std::vector<float> hz(12,110);
  for(unsigned i=0;i<9;++i){hz.push_back(0);hz.push_back(i%2?220.f:55.f);}
  hz.insert(hz.end(),rest,0);hz.insert(hz.end(),17,440);
  hz.insert(hz.end(),rest,0);hz.insert(hz.end(),19,110);
  for(unsigned null_white:{0U,1U}){
   midi_ddsp::Synth full,fast;full.reset();fast.reset();
   float raw[126],next[126],white[64],a[3][64],b[3][64];
   fill(raw,126);fill(white,64);full.prime(raw,white);fast.prime(raw,white);
   for(unsigned f=0;f+1<hz.size();++f){
    fill(next,126);fill(white,64);
    const float *future=(null_white&&f%23==0)?nullptr:white;
    full.interval(raw,next,hz[f],hz[f+1],future,a[0],a[1],a[2]);
    if(hz[f]==0&&hz[f+1]==0){fast.advance_muted_zero(next,future);++skipped;}
    else {
     fast.interval(raw,next,hz[f],hz[f+1],future,b[0],b[1],b[2]);
     for(unsigned c=0;c<3;++c)for(unsigned j=0;j<64;++j){
      assert(std::isfinite(b[c][j]));maximum=std::max(maximum,std::abs(a[c][j]-b[c][j]));
      assert(std::abs(a[c][j]-b[c][j])<1e-7f);++compared;
     }
    }
    std::memcpy(raw,next,sizeof(raw));
   }
   // A reset must also discard any previously skipped stream's noise state.
   full.reset();fast.reset();fill(raw,126);fill(white,64);full.prime(raw,white);fast.prime(raw,white);
   fill(next,126);fill(white,64);full.interval(raw,next,55,220,white,a[0],a[1],a[2]);
   fast.interval(raw,next,55,220,white,b[0],b[1],b[2]);assert(std::memcmp(a,b,sizeof(a))==0);
  }
 }
 std::printf("PASS: muted rest state; skipped=%u compared=%u max_abs=%.9g\n",skipped,compared,double(maximum));
}
