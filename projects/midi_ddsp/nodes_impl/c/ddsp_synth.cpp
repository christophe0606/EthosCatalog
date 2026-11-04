// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "ddsp_synth.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#if defined(MIDI_DDSP_MVE_OSCILLATOR) && defined(__ARM_FEATURE_MVE) && (__ARM_FEATURE_MVE & 2)
#include "../helium/oscillator_mve.hpp"
#endif
#ifdef MIDI_DDSP_CMSIS_DSP
#include "arm_math.h"
#endif
namespace midi_ddsp {
namespace {
constexpr double Tau=6.283185307179586476925286766559;
float scale_exact(float x){return 2.f*std::pow(1.f/(1.f+std::exp(-x)),float(std::log(10.)))+1e-7f;}
float scale(float x){
 // Cubic Hermite interpolation of the source sigmoid/power transform. Store
 // the derivative per table interval so each audio frame avoids exp/pow.
 // Values outside the table, including nonfinite inputs, retain source math.
 constexpr unsigned N=2048;
 static float values[N+1],slopes[N+1];static bool ready=false;
 if(!ready){
  const float exponent=float(std::log(10.));
  for(unsigned i=0;i<=N;++i){
   const float input=float(i)/64-16,sigmoid=1.f/(1.f+std::exp(-input));
   const float y=2.f*std::pow(sigmoid,exponent);
   values[i]=y+1e-7f;slopes[i]=y*exponent*(1-sigmoid)/64;
  }
  ready=true;
 }
 if(!(x>=-16&&x<16))return scale_exact(x);
 const float coordinate=(x+16)*64;const unsigned index=unsigned(coordinate);
 if(index>=N)return scale_exact(x);
 const float t=coordinate-index,delta=values[index+1]-values[index];
 const float a=slopes[index],b=slopes[index+1];
 return values[index]+t*(a+t*((3*delta-2*a-b)+t*(a+b-2*delta)));
}
// Dense interpolated sine table. Generated from the mathematical function,
// owned by this project; no third-party table or phase reset is introduced.
const float *sine_table(){
 constexpr unsigned Size=8192;static float table[Size+1];static bool ready=false;
 if(!ready){for(unsigned i=0;i<=Size;++i)table[i]=float(std::sin(Tau*i/Size));ready=true;}
 return table;
}
float sine(float phase){
 constexpr unsigned Size=8192;const float *table=sine_table();
 float coordinate=phase*float(Size/Tau);unsigned index=unsigned(coordinate);
 if(index>=Size){index=0;coordinate=0;}
 float t=coordinate-index;
 const float y0=table[index],y1=table[index+1];
 return y0+t*(y1-y0);
}
void envelope(const float *raw,float hz,float *out){
 float total=0;for(unsigned h=0;h<60;++h){out[h]=hz*(h+1)>=8000?0:scale(raw[h+1]);total+=out[h];}
 const float amplitude=scale(raw[0])/(total==0?1e-7f:total);
 for(unsigned h=0;h<60;++h)out[h]*=amplitude;
}
}
void real_fft128(float *input,float *output,bool inverse){
#ifdef MIDI_DDSP_CMSIS_DSP
 static arm_rfft_fast_instance_f32 instance;
 static bool initialized=false;
 if(!initialized){arm_rfft_fast_init_128_f32(&instance);initialized=true;}
 arm_rfft_fast_f32(&instance,input,output,inverse?1:0);
#else
 double real[128],imag[128]{};
 if(!inverse){for(unsigned j=0;j<128;++j)real[j]=input[j];}
 else {real[0]=input[0];real[64]=input[1];for(unsigned j=1;j<64;++j){real[j]=real[128-j]=input[j*2];imag[j]=input[j*2+1];imag[128-j]=-imag[j];}}
 for(unsigned j=1,k=0;j<128;++j){unsigned bit=64;for(;k&bit;bit>>=1)k^=bit;k^=bit;if(j<k){std::swap(real[j],real[k]);std::swap(imag[j],imag[k]);}}
 for(unsigned width=2;width<=128;width*=2)for(unsigned base=0;base<128;base+=width)for(unsigned j=0;j<width/2;++j){const double angle=(inverse?Tau:-Tau)*j/width,c=std::cos(angle),s=std::sin(angle);const unsigned a=base+j,b=a+width/2;double r=real[b]*c-imag[b]*s,i=real[b]*s+imag[b]*c;real[b]=real[a]-r;imag[b]=imag[a]-i;real[a]+=r;imag[a]+=i;}
 if(inverse){for(unsigned j=0;j<128;++j)output[j]=float(real[j]/128);}
 else {output[0]=float(real[0]);output[1]=float(real[64]);for(unsigned j=1;j<64;++j){output[j*2]=float(real[j]);output[j*2+1]=float(imag[j]);}}
#endif
}
void Synth::reset(){std::memset(phase_,0,sizeof(phase_));std::memset(noise_,0,sizeof(noise_));}
void Synth::add_noise(const float *raw,const float *white,unsigned offset){
 float spectrum[128]{},impulse[128];
 spectrum[0]=scale(raw[61]-5);spectrum[1]=scale(raw[125]-5);
 for(unsigned j=1;j<64;++j)spectrum[2*j]=scale(raw[61+j]-5);
 real_fft128(spectrum,impulse,true);
 // Window in zero-phase order, then fftshift into causal order.
 float causal[128];for(unsigned j=0;j<128;++j){unsigned source=(j+64)%128;causal[j]=impulse[source]*float(.5+.5*std::cos(Tau*source/128));}
 for(unsigned j=0;j<64;++j)for(unsigned k=0;k<128;++k)noise_[offset+j+k]+=white[j]*causal[k];
}
void Synth::prime(const float *raw,const float *white){add_noise(raw,white,0);}
void Synth::advance_muted_zero(const float *next,const float *next_white){
 if(next_white)add_noise(next,next_white,64);
 std::memmove(noise_,noise_+64,192*4);std::memset(noise_+192,0,64*4);
}
void Synth::interval(const float *raw,const float *next,float hz,float next_hz,const float *next_white,float *harmonic,float *noise,float *dry){
#if defined(__clang__)
 // Compensated phase addition relies on this operation order even in the
 // Release build, whose other neural/DSP loops permit fast-math reassociation.
#pragma clang fp reassociate(off)
#pragma clang fp contract(off)
#endif
 float a[60],b[60];envelope(raw,hz,a);envelope(next,next_hz,b);
 if(next_white)add_noise(next,next_white,64);
 static float window[64];static bool initialized=false;
 if(!initialized){for(unsigned s=0;s<64;++s)window[s]=float(.5-.5*std::cos(3.14159265358979323846*s/64));initialized=true;}
 std::memset(harmonic,0,64*4);
#if defined(MIDI_DDSP_MVE_OSCILLATOR) && defined(__ARM_FEATURE_MVE) && (__ARM_FEATURE_MVE & 2)
 midi_ddsp_oscillator_mve(a,b,hz,next_hz,phase_,sine_table(),window,harmonic);
#else
 for(unsigned h=0;h<60;++h){
  const float first=hz*float(h+1),last=next_hz*float(h+1);
  float phase=float(phase_[h]),correction=0,integral_hi=0,integral_lo=0;
  const bool masked=a[h]==0&&b[h]==0;
  for(unsigned s=0;s<64;++s){
   const float fraction=float(s)/64;
   const float frequency=first*(1-fraction)+last*fraction;
   // FastTwoSum expansion: positive frequencies and a monotonically growing
   // sum make hi>=frequency after the first term. Retain rounding residuals
   // in lo, then combine in double only once at the control boundary.
   const float total=integral_hi+frequency;
   integral_lo+=frequency-(total-integral_hi);integral_hi=total;
   if(masked)continue;
   float increment=frequency*float(Tau/16000)-correction;
   float updated=phase+increment;correction=(updated-phase)-increment;phase=updated;
   const unsigned turns=unsigned(phase*float(1/Tau));phase-=float(turns)*float(Tau);
   if(frequency<8000)harmonic[s]+=sine(phase)*(a[h]*(1-window[s])+b[h]*window[s]);
  }
  // Reset local float rounding at each boundary using the exact sum of the
  // oracle's rounded interpolated frequencies. Persistent phase never resets.
  const double integral=double(integral_hi)+double(integral_lo);
  phase_[h]=std::fmod(phase_[h]+integral*(Tau/16000),Tau);
 }
#endif
 for(unsigned s=0;s<64;++s){noise[s]=noise_[62+s];dry[s]=harmonic[s]+noise[s];}
 std::memmove(noise_,noise_+64,192*4);std::memset(noise_+192,0,64*4);
}
void Reverb::init(float *coefficients,float *history,const float *impulse,bool learned){
 coefficients_=coefficients;history_=history;position_=0;std::memset(history,0,750*128*4);std::memset(overlap_,0,sizeof(overlap_));
 for(unsigned p=0;p<750;++p){float block[128]{};for(unsigned j=0;j<64;++j){unsigned n=p*64+j;block[j]=impulse[n];if(learned){if(n==0)block[j]=0;else if(n>=16000)block[j]*=float(std::exp(-4.*(n-16000)/31999.));}}real_fft128(block,coefficients+p*128,false);}
}
void Reverb::process(const float *dry,float *wet){
 float block[128]{},sum[128]{};std::memcpy(block,dry,64*4);real_fft128(block,history_+position_*128,false);
 for(unsigned p=0;p<750;++p){const float *h=coefficients_+p*128,*x=history_+((position_+750-p)%750)*128;
  sum[0]+=h[0]*x[0];sum[1]+=h[1]*x[1];
  for(unsigned j=2;j<128;j+=2){sum[j]+=h[j]*x[j]-h[j+1]*x[j+1];sum[j+1]+=h[j]*x[j+1]+h[j+1]*x[j];}
 }
 real_fft128(sum,block,true);for(unsigned j=0;j<64;++j){wet[j]=dry[j]+block[j]+overlap_[j];overlap_[j]=block[j+64];}position_=(position_+1)%750;
}
}
