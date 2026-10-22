// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
// New implementation of the recurrence documented in model/ADAPTER_CONTRACT.md.
#include "handwriting_engine.hpp"
#include "embedded_runtime.hpp"
#include "cmsis_os2.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
namespace handwriting {
const char alphabet[]="\0 !\"#'(),-.0123456789:;?ABCDEFGHIJKLMNOPRSTUVWYabcdefghijklmnopqrstuvwxyz";
const char *const method_names[5]={"lstm1","attention","lstm2","lstm3","gmm"};
const unsigned input_sizes[5]={476,476,876,876,400}, output_sizes[5]={1600,30,1600,1600,121};
static bool is_finite(float x) {
    uint32_t bits; std::memcpy(&bits,&x,4); return (bits&0x7f800000U)!=0x7f800000U;
}
static float sigmoid(float x) { return 1.0f/(1.0f+std::exp(-x)); }
static float softplus(float x) { return std::max(x,0.0f)+std::log1p(std::exp(-std::fabs(x))); }
int Engine::dense(unsigned method,const float *input,float *output,uint32_t *ticks) {
    using namespace executorch::aten;
    if(method>=5 || !module_ || !input || !output) return -420;
    // Caller input can reside in ordinary RAM; boundary quantization produces
    // the NPU input in the runtime's NPU-visible planned pool.
    std::array<SizesType,2> sizes{1,static_cast<SizesType>(input_sizes[method])};
    std::array<DimOrderType,2> order{0,1};
    TensorImpl impl(ScalarType::Float,2,sizes.data(),const_cast<float*>(input),order.data());
    const uint32_t start=osKernelGetSysTimerCount();
    auto result=module_->execute(method_names[method],Tensor(&impl));
    if(!result.ok() || result->size()!=1 || !result->front().isTensor()) return -421;
    Tensor out=result->front().toTensor();
    if(out.scalar_type()!=ScalarType::Float || out.numel()!=output_sizes[method]) return -422;
    std::memcpy(output,out.const_data_ptr<float>(),output_sizes[method]*sizeof(float));
    if(ticks) *ticks=osKernelGetSysTimerCount()-start;
    for(unsigned i=0;i<output_sizes[method];++i) if(!is_finite(output[i])) return -423;
    return 0;
}
int Engine::begin(const char *sentence,uint32_t seed) {
    if(!sentence) return -424;
    const size_t length=std::strlen(sentence);
    if(!length || length>MaxCharacters) return -424;
    State clean{};
    for(unsigned i=0;i<length;++i) {
        const char *found=std::strchr(alphabet+1,sentence[i]);
        if(!found) return -424;
        clean.characters[i]=static_cast<uint8_t>(found-alphabet);
    }
    clean.length=length+1; clean.limit=40*length;
    clean.previous={0,0,1}; clean.rng=seed?seed:1;
    state_=clean; return 0;
}
void Engine::cell(const float *g,float *h,float *c) {
    for(unsigned i=0;i<Hidden;++i) {
        c[i]=sigmoid(g[800+i]+1)*c[i]+sigmoid(g[i])*std::tanh(g[400+i]);
        h[i]=std::tanh(c[i])*sigmoid(g[1200+i]);
    }
}
void Engine::window(const float *a,State &s) {
    float alpha[10],beta[10];
    for(unsigned j=0;j<10;++j) {
        alpha[j]=softplus(a[j]); beta[j]=std::max(softplus(a[10+j]),.01f);
        s.kappa[j]+=softplus(a[20+j])/25.0f;
    }
    std::fill(s.window,s.window+AlphabetSize,0);
    for(unsigned u=0;u<s.length;++u) {
        float phi=0;
        for(unsigned j=0;j<10;++j) { float d=s.kappa[j]-u; phi+=alpha[j]*std::exp(-d*d/beta[j]); }
        s.phi[u]=phi; s.window[s.characters[u]]+=phi;
    }
}
float Engine::uniform() {
    uint32_t x=state_.rng; x^=x<<13; x^=x>>17; x^=x<<5; state_.rng=x;
    // Strictly inside (0,1), including after float rounding; safe Box-Muller.
    return (static_cast<float>(x>>9)+.5f)/8388608.0f;
}
bool Engine::finite() const {
    for(unsigned k=0;k<3;++k) for(unsigned j=0;j<400;++j)
        if(!is_finite(state_.h[k][j]) || !is_finite(state_.c[k][j])) return false;
    for(float x:state_.window) if(!is_finite(x)) return false;
    for(float x:state_.kappa) if(!is_finite(x)) return false;
    return is_finite(state_.previous.dx)&&is_finite(state_.previous.dy);
}
int Engine::step(Stroke &point) {
    auto &s=state_;
    if(!s.length) return -424;
    if(s.steps>=s.limit) return 2;
    float xy[3]={s.previous.dx,s.previous.dy,s.previous.pen_up};
    std::memcpy(input_,s.window,73*4); std::memcpy(input_+73,xy,3*4);
    std::memcpy(input_+76,s.h[0],400*4);
    int error=dense(0,input_,output_); if(error) return error;
    cell(output_,s.h[0],s.c[0]);
    std::memcpy(input_+76,s.h[0],400*4);
    error=dense(1,input_,output_); if(error) return error;
    window(output_,s);
    for(unsigned k=1;k<3;++k) {
        std::memcpy(input_,xy,3*4); std::memcpy(input_+3,s.h[k-1],400*4);
        std::memcpy(input_+403,s.window,73*4); std::memcpy(input_+476,s.h[k],400*4);
        error=dense(k+1,input_,output_); if(error) return error;
        cell(output_,s.h[k],s.c[k]);
    }
    error=dense(4,s.h[2],output_); if(error) return error;
    float pen=sigmoid(output_[120]);
    pen=pen<.01f?0:std::clamp(pen,1e-8f,1.0f-1e-8f);
    unsigned best=0; for(unsigned u=1;u<s.length;++u) if(s.phi[u]>s.phi[best]) best=u;
    ++s.steps;
    // The source termination pen draw is independent of the emitted pen draw.
    if(best==s.length-1 && uniform()<pen) return 1;
    float p[20],total=0,maxlog=output_[0]*1.5f;
    for(unsigned k=1;k<20;++k) maxlog=std::max(maxlog,output_[k]*1.5f);
    for(unsigned k=0;k<20;++k) { p[k]=std::exp(output_[k]*1.5f-maxlog); total+=p[k]; }
    float threshold_total=0;
    for(unsigned k=0;k<20;++k) { p[k]/=total; if(p[k]<.01f) p[k]=0; threshold_total+=p[k]; }
    float draw=uniform()*threshold_total,cumulative=0; unsigned chosen=19;
    for(unsigned k=0;k<20;++k) { cumulative+=p[k]; if(draw<cumulative) { chosen=k; break; } }
    float radius=std::sqrt(-2.0f*std::log(uniform())), angle=6.28318530718f*uniform();
    float z1=radius*std::cos(angle),z2=radius*std::sin(angle);
    float sx=std::max(std::exp(output_[20+chosen]-.5f),.0001f);
    float sy=std::max(std::exp(output_[40+chosen]-.5f),.0001f);
    // 1e-8 rounds to zero at 1 in float32. Use the nearest interior float.
    float rho=std::clamp(std::tanh(output_[60+chosen]),-.9999999403953552f,.9999999403953552f);
    point={output_[80+chosen]+sx*z1,
        output_[100+chosen]+sy*(rho*z1+std::sqrt(std::max(0.0f,1-rho*rho))*z2),
        uniform()<pen?1.0f:0.0f};
    s.previous=point;
    return finite()?0:-425;
}
}
