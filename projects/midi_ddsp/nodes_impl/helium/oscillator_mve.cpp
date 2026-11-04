// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "oscillator_mve.hpp"
#if defined(__ARM_FEATURE_MVE) && (__ARM_FEATURE_MVE & 2)
#include <arm_mve.h>
#include <cmath>
extern "C" void midi_ddsp_oscillator_mve(const float *a,const float *b,float hz,
 float next_hz,double *persistent,const float *table,const float *window,float *out){
#pragma clang fp reassociate(off)
#pragma clang fp contract(off)
 constexpr double Tau=6.283185307179586476925286766559;
 constexpr float Rate=float(Tau/16000),InverseTau=float(1/Tau),TableScale=float(8192/Tau);
 const float32x4_t zero=vdupq_n_f32(0);
 for(unsigned h=0;h<60;h+=4){
  float first_values[4],last_values[4],phase_values[4];
  for(unsigned k=0;k<4;++k){first_values[k]=hz*float(h+k+1);last_values[k]=next_hz*float(h+k+1);phase_values[k]=float(persistent[h+k]);}
  const float32x4_t first=vld1q_f32(first_values),last=vld1q_f32(last_values);
  float32x4_t phase=vld1q_f32(phase_values),correction=zero,hi=zero,lo=zero;
  const float32x4_t av=vld1q_f32(a+h),bv=vld1q_f32(b+h);
  for(unsigned s=0;s<64;++s){
   float fraction=float(s)/64;
   float32x4_t frequency=vaddq_f32(vmulq_n_f32(first,1-fraction),vmulq_n_f32(last,fraction));
   float32x4_t total=vaddq_f32(hi,frequency);
   lo=vaddq_f32(lo,vsubq_f32(frequency,vsubq_f32(total,hi)));hi=total;
   float32x4_t increment=vsubq_f32(vmulq_n_f32(frequency,Rate),correction);
   float32x4_t updated=vaddq_f32(phase,increment);
   correction=vsubq_f32(vsubq_f32(updated,phase),increment);phase=updated;
   uint32x4_t turns=vcvtq_u32_f32(vmulq_n_f32(phase,InverseTau));
   phase=vsubq_f32(phase,vmulq_n_f32(vcvtq_f32_u32(turns),float(Tau)));
   float32x4_t coordinate=vmulq_n_f32(phase,TableScale);
   uint32x4_t index=vcvtq_u32_f32(coordinate);
   mve_pred16_t valid=vcmpleq_n_s32(vreinterpretq_s32_u32(index),8191);
   index=vpselq_u32(index,vdupq_n_u32(0),valid);coordinate=vpselq_f32(coordinate,zero,valid);
   float32x4_t y0=vldrwq_gather_shifted_offset_f32(table,index);
   float32x4_t y1=vldrwq_gather_shifted_offset_f32(table,vaddq_n_u32(index,1));
   float32x4_t t=vsubq_f32(coordinate,vcvtq_f32_u32(index));
   float32x4_t sine=vaddq_f32(y0,vmulq_f32(t,vsubq_f32(y1,y0)));
   float32x4_t amplitude=vaddq_f32(vmulq_n_f32(av,1-window[s]),vmulq_n_f32(bv,window[s]));
   float32x4_t contribution=vpselq_f32(vmulq_f32(sine,amplitude),zero,vcmpltq_n_f32(frequency,8000));
   // Retain the scalar harmonic addition order; MVE has no F32 across-add.
   out[s]+=vgetq_lane_f32(contribution,0);out[s]+=vgetq_lane_f32(contribution,1);
   out[s]+=vgetq_lane_f32(contribution,2);out[s]+=vgetq_lane_f32(contribution,3);
  }
  float high[4],low[4];vst1q_f32(high,hi);vst1q_f32(low,lo);
  for(unsigned k=0;k<4;++k)persistent[h+k]=std::fmod(persistent[h+k]+(double(high[k])+double(low[k]))*(Tau/16000),Tau);
 }
}
#endif
