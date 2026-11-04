// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "rnnoise_dsp.h"
#include "RNNoiseFeatureProcessor.hpp"
#include <cstring>
#include <new>
using arm::app::rnn::RNNoiseFeatureProcessor;
using arm::app::rnn::FrameFeatures;
struct State {RNNoiseFeatureProcessor processor;FrameFeatures f;};
extern "C" {
void* rn_dsp_create(){return new(std::nothrow) State;}
void rn_dsp_destroy(void* p){delete static_cast<State*>(p);}
void rn_dsp_reset(void* p){static_cast<State*>(p)->processor.Reset();}
void rn_analyze(void* p,const int16_t* pcm,float* features,float* context,uint32_t epoch){
 auto& s=*static_cast<State*>(p);float audio[480];float energy=0;for(unsigned i=0;i<480;++i){audio[i]=pcm[i];energy+=audio[i]*audio[i];}
 s.processor.PreprocessFrame(audio,480,s.f);std::memcpy(features,s.f.m_featuresVec.data(),42*4);
 float* c=context;for(auto* v:{&s.f.m_fftX,&s.f.m_fftP,&s.f.m_Ex,&s.f.m_Ep,&s.f.m_Exp}){std::memcpy(c,v->data(),v->size()*4);c+=v->size();}
 *c++=s.f.m_silence?1.f:0.f;*c++=float(epoch);*c=std::sqrt(energy/480)/32768.f;
}
void rn_synthesize(void* p,const float* c,const float* gains,float* output){
 auto& s=*static_cast<State*>(p);s.f.m_fftX.assign(c,c+962);c+=962;s.f.m_fftP.assign(c,c+962);c+=962;s.f.m_Ex.assign(c,c+22);c+=22;s.f.m_Ep.assign(c,c+22);c+=22;s.f.m_Exp.assign(c,c+22);c+=22;s.f.m_silence=(*c++!=0);++c;float rms=*c;
 std::vector<float> g(gains,gains+22),audio(480);s.processor.PostProcessFrame(g,s.f,audio);
 float mean=0;for(unsigned i=0;i<22;++i)mean+=gains[i]/22.f;
 for(unsigned i=0;i<480;++i)output[i]=audio[i]/32768.f;
 output[480]=rms;output[481]=gains[22];output[482]=mean;
}
}
