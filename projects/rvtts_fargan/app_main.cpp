// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "rvtts_engine.hpp"
#include "embedded_runtime.hpp"
#include "model/model_test_data.h"
#include "model/benchmark_inputs.h"
#include "model_pte.h"
#include "cmsis_os2.h"
#include "main.h"
#include "se_services_port.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
extern "C" {
volatile int32_t pipeline_error;
volatile uint32_t rvtts_benchmark_complete,rvtts_benchmark_stage,rvtts_numeric_failures;
rvtts::Timing rvtts_method_timing[6][10],rvtts_sequence_timing[3][3];
float rvtts_method_error[6],rvtts_sequence_error[3];
uint32_t rvtts_method_weights[6],rvtts_method_arena[6];
}
namespace {
alignas(32) float output[rvtts::MaxFrames*20] __attribute__((section(APP_POOL_SECTION)));
alignas(32) int16_t pcm[(rvtts::MaxFrames+1)*160] __attribute__((section(APP_POOL_SECTION)));
uint32_t cpu_hz,axi_hz,timer_hz;
float maximum_error(const float *a,const float *b,unsigned count) {
    float maximum=0;
    for(unsigned i=0;i<count;++i) {
        uint32_t bits;std::memcpy(&bits,a+i,4);
        if((bits&0x7f800000U)==0x7f800000U)return 1e30f;
        maximum=std::max(maximum,std::fabs(a[i]-b[i]));
    }
    return maximum;
}
int benchmark() {
    uint32_t service_error=0;
    if(SERVICES_clocks_setting_get(se_services_s_handle,CLOCK_SETTING_EXTSYS0_FREQ,&cpu_hz,&service_error)!=SERVICES_REQ_SUCCESS||service_error||!cpu_hz)return -410;
    if(SERVICES_clocks_setting_get(se_services_s_handle,CLOCK_SETTING_AXI_FREQ,&axi_hz,&service_error)!=SERVICES_REQ_SUCCESS||service_error||!axi_hz)return -410;
    timer_hz=osKernelGetSysTimerFreq();if(!timer_hz)return -410;
    printf("rvTTS FARGAN benchmark clocks CPU=%lu AXI=%lu timer=%lu model=%lu\n",(unsigned long)cpu_hz,(unsigned long)axi_hz,(unsigned long)timer_hz,(unsigned long)model_pte_size);
    const float *inputs[]={rvtts_prosody_pre_input,rvtts_prosody_gru_input,rvtts_prosody_head_input,rvtts_acoustic_input,rvtts_fargan_init_input,rvtts_fargan_frame_input};
    const float *expected[]={rvtts_prosody_pre_expected,rvtts_prosody_gru_expected,rvtts_prosody_head_expected,rvtts_acoustic_expected,rvtts_fargan_init_expected,rvtts_fargan_frame_expected};
    const float tolerance[]={RVTTS_PROSODY_PRE_ATOL+1e-6f,RVTTS_PROSODY_GRU_ATOL+1e-6f,RVTTS_PROSODY_HEAD_ATOL+1e-6f,RVTTS_ACOUSTIC_ATOL+1e-6f,RVTTS_FARGAN_INIT_ATOL+1e-6f,RVTTS_FARGAN_FRAME_ATOL+1e-6f};
    for(unsigned method=0;method<6;++method) {
        rvtts_benchmark_stage=10+method;
        for(unsigned sample=0;sample<=10;++sample) {
            rvtts::Timing timing{};
            int error=method<4?rvtts::execute(method,inputs[method],output,&timing):rvtts::vocoder_method(method,inputs[method],method==rvtts::Init?rvtts_fargan_init_indices:rvtts_fargan_frame_indices,output,&timing);
            if(error)return error;
            rvtts_method_error[method]=std::max(rvtts_method_error[method],maximum_error(output,expected[method],rvtts::output_sizes[method]));
            if(sample)rvtts_method_timing[method][sample-1]=timing;
        }
        if(rvtts_method_error[method]>tolerance[method])++rvtts_numeric_failures;
        rvtts_method_weights[method]=ethos_benchmark.weights_size;rvtts_method_arena[method]=ethos_benchmark.arena_size;
        printf("Method %s error=%.9f tolerance=%.9f weights=%lu arena=%lu\n",rvtts::names[method],rvtts_method_error[method],tolerance[method],(unsigned long)ethos_benchmark.weights_size,(unsigned long)ethos_benchmark.arena_size);
        for(unsigned sample=0;sample<10;++sample) {
            const auto &t=rvtts_method_timing[method][sample];
            printf("MethodSample %s %u ticks=%llu cycles=%llu invocations=%lu overflow=%lu\n",rvtts::names[method],sample+1,(unsigned long long)t.ticks,(unsigned long long)t.cycles,(unsigned long)t.invocations,(unsigned long)t.overflow);
        }
        fflush(stdout);
    }
    const unsigned lengths[]={2,19,81};
    const uint8_t *phones[]={rvtts_sequence_2_phones,rvtts_sequence_19_phones,rvtts_sequence_81_phones};
    const uint8_t *stress[]={rvtts_sequence_2_stress,rvtts_sequence_19_stress,rvtts_sequence_81_stress};
    const uint16_t *words[]={rvtts_sequence_2_words,rvtts_sequence_19_words,rvtts_sequence_81_words};
    const float *references[]={rvtts_sequence_2_features,rvtts_sequence_19_features,rvtts_sequence_81_features};
    const float *pcm_reference[]={rvtts_sequence_2_pcm,rvtts_sequence_19_pcm,rvtts_sequence_81_pcm};
    for(unsigned sequence=0;sequence<3;++sequence) {
        rvtts_benchmark_stage=20+sequence;
        printf("SequenceStart frames=%u\n",lengths[sequence]);fflush(stdout);
        unsigned samples=0;
        for(unsigned sample=0;sample<=3;++sample) {
            rvtts::Timer start;rvtts::Timing feature{},vocoder{};
            int error=rvtts::features(phones[sequence],stress[sequence],words[sequence],lengths[sequence],output,&feature);
            if(error)return error;
            const uint64_t feature_end=start.elapsed();
            // Reference comparison is outside both measured schedule intervals.
            rvtts_sequence_error[sequence]=std::max(rvtts_sequence_error[sequence],maximum_error(output,references[sequence],lengths[sequence]*20));
            rvtts::Timer vocoder_start;
            error=rvtts::synthesize(output,lengths[sequence],pcm,sizeof(pcm)/sizeof(pcm[0]),&samples,&vocoder);
            if(error)return error;
            rvtts::Timing total{feature_end+vocoder_start.elapsed(),feature.cycles+vocoder.cycles,feature.invocations+vocoder.invocations,feature.overflow+vocoder.overflow};
            if(sample)rvtts_sequence_timing[sequence][sample-1]=total;
            if(sample==1)for(unsigned frame=0;frame<lengths[sequence]+2;++frame) {
                const auto &t=rvtts::vocoder_full_frame_timing[frame];
                printf("FullFrameSample %u %u ticks=%llu cycles=%llu invocations=%lu overflow=%lu\n",lengths[sequence],frame,(unsigned long long)t.ticks,(unsigned long long)t.cycles,(unsigned long)t.invocations,(unsigned long)t.overflow);
            }
            printf("SequenceProgress frames=%u sample=%u ticks=%llu\n",lengths[sequence],sample,(unsigned long long)total.ticks);fflush(stdout);
        }
        if(rvtts_sequence_error[sequence]>.01f)++rvtts_numeric_failures;
        double squared=0,reference_squared=0;float pcm_error=0;unsigned clipped=0;
        for(unsigned i=0;i<samples;++i) {
            const float value=pcm[i]/32768.0f,reference=pcm_reference[sequence][i],delta=value-reference;
            squared+=double(delta)*delta;reference_squared+=double(reference)*reference;pcm_error=std::max(pcm_error,std::fabs(delta));
            if(pcm[i]==32767||pcm[i]==-32768)++clipped;
        }
        const double rmse=std::sqrt(squared/samples),rms=std::sqrt(reference_squared/samples);
        const double snr=20*std::log10(std::max(rms,1e-20)/std::max(rmse,1e-20));
        printf("Sequence frames=%u samples=%u audio_ms=%u max_error=%.9f tolerance=0.01\n",lengths[sequence],samples,samples/16,rvtts_sequence_error[sequence]);
        printf("PCM frames=%u max_error=%.9f rmse=%.9f reference_rms=%.9f snr_db=%.6f clipped=%u\n",lengths[sequence],pcm_error,rmse,rms,snr,clipped);
        for(unsigned sample=0;sample<3;++sample) {
            const auto &t=rvtts_sequence_timing[sequence][sample];
            printf("SequenceSample %u %u ticks=%llu cycles=%llu invocations=%lu overflow=%lu\n",lengths[sequence],sample+1,(unsigned long long)t.ticks,(unsigned long long)t.cycles,(unsigned long)t.invocations,(unsigned long)t.overflow);
        }
        fflush(stdout);
    }
    auto usage=arm::embedded::runtime_memory_usage();
    printf("Pools method=%lu temp=%lu reserved=%lu\n",(unsigned long)usage.method_peak,(unsigned long)usage.temp_peak,(unsigned long)(APP_METHOD_POOL_SIZE+APP_TEMP_POOL_SIZE));
    printf("Scope: methods execute only; full frame includes input pitch/embedding, state copies, de-emphasis and PCM conversion. Complete schedule includes shared feature inference, pitch normalization, init, N+2 frame calls, delay/flush and PCM16; excludes text frontend, UART, LCD and I2S.\n");
    rvtts_benchmark_complete=1;rvtts_benchmark_stage=30;
    printf("Numeric_failures: %lu\nTest_result: %s\n",(unsigned long)rvtts_numeric_failures,rvtts_numeric_failures?"FAIL":"PASS");fflush(stdout);
    return rvtts_numeric_failures?-470:0;
}
}
extern "C" int app_main(void) {
    rvtts_benchmark_stage=1;int error=rvtts::initialize();
    if(!error){rvtts_benchmark_stage=2;error=benchmark();}
    pipeline_error=error;
    if(error){printf("rvTTS FARGAN benchmark failed error=%ld stage=%lu\n",(long)error,(unsigned long)rvtts_benchmark_stage);fflush(stdout);}
    return error;
}
