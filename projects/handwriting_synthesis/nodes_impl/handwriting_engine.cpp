// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
// New implementation of the recurrence documented in model/ADAPTER_CONTRACT.md.
#include "handwriting_engine.hpp"
#include <algorithm>
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
    state_=clean; next_method_=0; return 0;
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
    std::fill(s.window,s.window+AlphabetSize,0.0f);
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
    for(unsigned method=0;method<MethodCount;++method) {
        int error=stage(method,point); if(error) return error;
    }
    return 0;
}
int Engine::stage(unsigned method,Stroke &point) {
    auto &s=state_;
    if(!s.length) return -424;
    if(method>=MethodCount || method!=next_method_) return -435;
    if(method==0 && s.steps>=s.limit) return 2;
    float xy[3]={s.previous.dx,s.previous.dy,s.previous.pen_up};
    if(method<=1) {
        // Attention reuses the old window/xy packed for lstm1. Only h0 changed.
        if(method==0) {
            std::memcpy(input_,s.window,73*sizeof(float));
            std::memcpy(input_+73,xy,3*sizeof(float));
        }
        std::memcpy(input_+76,s.h[0],400*4);
    } else if(method<=3) {
        const unsigned k=method-1;
        // lstm3 keeps the xy/new window packed for lstm2; both h slices change.
        if(method==2) {
            std::memcpy(input_,xy,3*sizeof(float));
            std::memcpy(input_+403,s.window,73*sizeof(float));
        }
        std::memcpy(input_+3,s.h[k-1],400*sizeof(float));
        std::memcpy(input_+476,s.h[k],400*sizeof(float));
    }
    const float *output=nullptr;
    int error=dense_view(method,method==4?s.h[2]:input_,output); if(error) return error;
    next_method_=(method+1)%MethodCount;
    if(method==1) window(output,s);
    else if(method<4) cell(output,s.h[method==0?0:method-1],s.c[method==0?0:method-1]);
    if(method<4) return 0;
    float pen=sigmoid(output[120]);
    pen=pen<.01f?0:std::clamp(pen,1e-8f,1.0f-1e-8f);
    unsigned best=0; for(unsigned u=1;u<s.length;++u) if(s.phi[u]>s.phi[best]) best=u;
    ++s.steps;
    // The source termination pen draw is independent of the emitted pen draw.
    if(best==s.length-1 && uniform()<pen) return 1;
    float p[20],total=0,maxlog=output[0]*1.5f;
    for(unsigned k=1;k<20;++k) maxlog=std::max(maxlog,output[k]*1.5f);
    for(unsigned k=0;k<20;++k) { p[k]=std::exp(output[k]*1.5f-maxlog); total+=p[k]; }
    float threshold_total=0;
    for(unsigned k=0;k<20;++k) { p[k]/=total; if(p[k]<.01f) p[k]=0; threshold_total+=p[k]; }
    float draw=uniform()*threshold_total,cumulative=0; unsigned chosen=19;
    for(unsigned k=0;k<20;++k) { cumulative+=p[k]; if(draw<cumulative) { chosen=k; break; } }
    float radius=std::sqrt(-2.0f*std::log(uniform())), angle=6.28318530718f*uniform();
    float z1=radius*std::cos(angle),z2=radius*std::sin(angle);
    float sx=std::max(std::exp(output[20+chosen]-.5f),.0001f);
    float sy=std::max(std::exp(output[40+chosen]-.5f),.0001f);
    // 1e-8 rounds to zero at 1 in float32. Use the nearest interior float.
    float rho=std::clamp(std::tanh(output[60+chosen]),-.9999999403953552f,.9999999403953552f);
    point={output[80+chosen]+sx*z1,
        output[100+chosen]+sy*(rho*z1+std::sqrt(std::max(0.0f,1-rho*rho))*z2),
        uniform()<pen?1.0f:0.0f};
    s.previous=point;
    return finite()?0:-425;
}
}
