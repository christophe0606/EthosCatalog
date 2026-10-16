// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include <cstdio>
#include <cmath>
#include <algorithm>
#include "app_sizes.h"
#define MFCC_BACKEND_ONLY
#include "../../common/nodes_impl/c/MFCC.hpp"
#undef MFCC_BACKEND_ONLY
#include "embedded_runtime.hpp"
#include "nodes_impl/embedded_kws.h"
#include "model/model_test_data.h"
#include "generated/speech_test.h"
#include "mfcc_tables.h"
#include "stream_init.hpp"
#include "main.h"
extern "C" {
volatile int32_t pipeline_error;
volatile uint32_t kws_test_passed,kws_mfcc_test_passed,kws_speech_test_passed;
volatile uint32_t kws_self_test_cases;
volatile uint32_t kws_mfcc_max_lsb,kws_network_max_lsb;
float kws_speech_probability;
int8_t kws_speech_scores[12];
}
namespace {
HardwareParams hardware;
int self_test() {
    int8_t output[12]; uint32_t ticks;
    for(unsigned test=0;test<KWS_TEST_CASES;++test) {
        int error=kws_forward_int8(hardware.module,kws_test_inputs[test],output,&ticks);
        if(error) return error;
        for(unsigned i=0;i<12;++i) {
            unsigned diff=std::abs(int(output[i])-int(kws_tflite_default[test][i]));
            if(diff>kws_network_max_lsb) kws_network_max_lsb=diff;
            if(diff>KWS_TOLERANCE_LSB) return -620;
        }
        ++kws_self_test_cases;
    }
    kws_test_passed=1;
    arm_rfft_fast_instance_f32 fft;
    if(mfcc_init(&kws_mfcc_config,&fft)) return -630;
    float frame[1024],spectrum[1026],mel[40],input[640],features[490];
    for(unsigned t=0;t<49;++t) {
        for(unsigned i=0;i<640;++i) input[i]=float(kws_speech_pcm[t*320+i])/32768.f;
        mfcc_compute(&kws_mfcc_config,&fft,input,frame,spectrum,mel,features+t*10);
    }
    for(unsigned i=0;i<490;++i) {
        int q=int(std::max(-128.f,std::min(127.f,std::round(features[i]/0.20109497010707855f-5))));
        unsigned diff=std::abs(q-int(kws_speech_mfcc[i]));
        if(diff>kws_mfcc_max_lsb) kws_mfcc_max_lsb=diff;
        if(diff>1) return -631;
    }
    kws_mfcc_test_passed=1;
    int error=kws_forward(hardware.module,features,kws_speech_scores,&ticks);
    if(error) return error;
    unsigned word=kws_classify(kws_speech_scores,&kws_speech_probability);
    if(word!=0 || kws_speech_probability<0.7f) return -632;
    kws_speech_test_passed=1;
    printf("KWS startup: %lu network cases PASS (max %lu LSB); MFCC PASS (max %lu LSB); down %.4f\n",
        (unsigned long)kws_self_test_cases,(unsigned long)kws_network_max_lsb,
        (unsigned long)kws_mfcc_max_lsb,kws_speech_probability);
    printf("KWS Stream: %d Hz, %d/%d MFCC, %d-frame context, overlap %d, inference every %d ms; event LCD fade 1000 ms\n",
        MIC_SAMPLE_RATE,KWS_FRAME_SAMPLES,MIC_BLOCK_SAMPLES,KWS_CONTEXT_FRAMES,
        KWS_MFCC_OVERLAP,KWS_INFERENCE_HOP_FRAMES*MIC_BLOCK_SAMPLES*1000/MIC_SAMPLE_RATE);
    printf("Test_result: PASS\n"); fflush(stdout);
    return 0;
}
}
extern "C" int app_main(void) {
    int error=hardware_params_init(&hardware);
    if(!error) error=self_test();
    if(!error) error=stream_configure_and_start(&hardware);
    if(error) { pipeline_error=error; stream_stats.error=error; app_result=error;
        printf("Test_result: FAIL (%d)\n",error); fflush(stdout); }
    return error;
}
