// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <vector>
#include <complex>
#include <cmath>
#include <cassert>
namespace arm { namespace app { namespace math {
enum class FftType {real,complex};
struct FftInstance {unsigned m_fftLen=0;FftType type;std::vector<std::complex<float>> roots;};
// Portable mixed-radix Cooley-Tukey, radix 2/3/5: RNNoise requires 960 points.
// No global mutable scratch and no architecture-specific instructions.
struct MathUtils {
 static float SineF32(float x){return std::sin(x);} static float CosineF32(float x){return std::cos(x);} static float SqrtF32(float x){return std::sqrt(x);}
 static void FftInitF32(unsigned n,FftInstance& p,FftType type){p.m_fftLen=n;p.type=type;p.roots.resize(n);for(unsigned i=0;i<n;++i){float a=-6.2831853071795864769f*i/n;p.roots[i]={std::cos(a),std::sin(a)};}}
 static void transform(std::complex<float>* out,const std::complex<float>* in,unsigned stride,unsigned n,const FftInstance& p){
  if(n==1){out[0]=*in;return;}unsigned r=n%2==0?2:n%3==0?3:5;assert(n%r==0);unsigned m=n/r;
  for(unsigned j=0;j<r;++j)transform(out+j*m,in+j*stride,stride*r,m,p);
  for(unsigned k=0;k<m;++k){std::complex<float> a[5];for(unsigned j=0;j<r;++j)a[j]=out[j*m+k];for(unsigned l=0;l<r;++l){std::complex<float> sum{};for(unsigned j=0;j<r;++j)sum+=a[j]*p.roots[(j*(k+l*m)*(p.m_fftLen/n))%p.m_fftLen];out[k+l*m]=sum;}}
 }
 static void FftF32(const std::vector<float>& x,std::vector<float>& y,const FftInstance& p){
  unsigned n=p.m_fftLen;std::vector<std::complex<float>> in(n),out(n);
  for(unsigned i=0;i<n;++i)in[i]=p.type==FftType::real?std::complex<float>(x[i],0):std::complex<float>(x[2*i],x[2*i+1]);
  transform(out.data(),in.data(),1,n,p);
  if(p.type==FftType::real){y.resize(n+2);y[0]=out[0].real();y[1]=out[n/2].real();for(unsigned i=1;i<n/2;++i){y[2*i]=out[i].real();y[2*i+1]=out[i].imag();}y[n]=y[n+1]=0;}
  else{y.resize(2*n);for(unsigned i=0;i<n;++i){y[2*i]=out[i].real();y[2*i+1]=out[i].imag();}}
 }
};
}}}
