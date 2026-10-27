// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
// Compare the actual target MVE implementation with preserved scalar rvTTS.
#include "lpcvoc_check.hpp"
#include "../upstream/lpcvoc.h"
#include "../model/benchmark_inputs.h"
#include "cmsis_os2.h"
#include "dsp/basic_math_functions.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

extern "C" {
void lpcvoc_reference_init(LpcVocState *);
void lpcvoc_reference_frame(LpcVocState *,const float *,int16_t *);
volatile uint32_t lpc_dsp_check_pass;
}
namespace {
constexpr unsigned Frames=157,Samples=Frames*160;
alignas(16) int16_t reference[Samples] __attribute__((section(APP_POOL_SECTION)));
alignas(16) int16_t optimized[Samples] __attribute__((section(APP_POOL_SECTION)));
uint32_t synth(bool opt,unsigned mode,int16_t *output) {
    LpcVocState state;
    const uint32_t start=osKernelGetSysTimerCount();
    if(opt)lpcvoc_init(&state);else lpcvoc_reference_init(&state);
    for(unsigned t=0;t<Frames;++t){
        float f[20];std::copy_n(rvtts_sequence_157_features+t*20,20,f);
        if(mode){f[18]=mode==1?1.5f:-1.494353f;f[19]=(t%53<26)?-.1f:.35f;}
        if(opt)lpcvoc_frame(&state,f,output+t*160);
        else lpcvoc_reference_frame(&state,f,output+t*160);
    }
    return osKernelGetSysTimerCount()-start;
}
}
int lpcvoc_optimization_check() {
    for(unsigned mode=0;mode<3;++mode){
        uint32_t base[3],fast[3];
        synth(false,mode,reference);synth(true,mode,optimized);
        for(unsigned repeat=0;repeat<3;++repeat){
            base[repeat]=synth(false,mode,reference);
            fast[repeat]=synth(true,mode,optimized);
        }
        std::sort(base,base+3);std::sort(fast,fast+3);
        double signal=0,noise=0;int peak=0;
        for(unsigned i=0;i<Samples;++i){
            const int delta=int(reference[i])-int(optimized[i]);
            peak=std::max(peak,std::abs(delta));
            signal+=double(reference[i])*reference[i];noise+=double(delta)*delta;
        }
        const double snr=noise?10*std::log10(signal/noise):999.0;
        printf("LPC_DSP mode=%u samples=%u timer_hz=%lu reference_ticks=%lu optimized_ticks=%lu max_pcm_error=%d snr_db=%.3f mve=%u cmsis_mvef=%u\r\n",
               mode,Samples,(unsigned long)osKernelGetSysTimerFreq(),
               (unsigned long)base[1],(unsigned long)fast[1],peak,snr,
#if defined(__ARM_FEATURE_MVE)
               unsigned(__ARM_FEATURE_MVE),
#else
               0U,
#endif
#if defined(ARM_MATH_MVEF)
               1U
#else
               0U
#endif
        );
        if(peak>64||snr<60.0)return -460;
    }
    lpc_dsp_check_pass=1;
    return 0;
}
