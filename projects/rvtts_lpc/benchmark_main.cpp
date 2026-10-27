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
volatile uint32_t rvtts_benchmark_complete,rvtts_benchmark_stage;
rvtts::Timing rvtts_method_timing[4][10],rvtts_sequence_timing[3][10];
float rvtts_method_error[4],rvtts_sequence_error[3];
uint32_t rvtts_method_weights[4],rvtts_method_arena[4];
}
namespace {
alignas(32) float output[rvtts::MaxFrames*20] __attribute__((section(APP_POOL_SECTION)));
uint32_t cpu_hz,axi_hz,timer_hz;
int compare(const float *a,const float *b,unsigned count,float tolerance,float &maximum) {
    for(unsigned i=0;i<count;++i){uint32_t bits;std::memcpy(&bits,a+i,4);float e=std::fabs(a[i]-b[i]);if((bits&0x7f800000U)==0x7f800000U )return -440;maximum=std::max(maximum,e);}return maximum>tolerance?-440:0;
}
int benchmark() {
    uint32_t service_error=0;
    if(SERVICES_clocks_setting_get(se_services_s_handle,CLOCK_SETTING_EXTSYS0_FREQ,&cpu_hz,&service_error)!=SERVICES_REQ_SUCCESS||service_error||!cpu_hz)return -410;
    if(SERVICES_clocks_setting_get(se_services_s_handle,CLOCK_SETTING_AXI_FREQ,&axi_hz,&service_error)!=SERVICES_REQ_SUCCESS||service_error||!axi_hz)return -410;
    timer_hz=osKernelGetSysTimerFreq();if(!timer_hz)return -410;
    printf("rvTTS LPC benchmark clocks CPU=%lu AXI=%lu timer=%lu model=%lu\n",(unsigned long)cpu_hz,(unsigned long)axi_hz,(unsigned long)timer_hz,(unsigned long)model_pte_size);
    const float *inputs[]={rvtts_prosody_pre_input,rvtts_prosody_gru_input,rvtts_prosody_head_input,rvtts_acoustic_input};
    const float *expected[]={rvtts_prosody_pre_expected,rvtts_prosody_gru_expected,rvtts_prosody_head_expected,rvtts_acoustic_expected};
    const float tolerance[]={RVTTS_PROSODY_PRE_ATOL+1e-6f,RVTTS_PROSODY_GRU_ATOL+1e-6f,RVTTS_PROSODY_HEAD_ATOL+1e-6f,RVTTS_ACOUSTIC_ATOL+1e-6f};
    for(unsigned method=0;method<4;++method){rvtts_benchmark_stage=10+method;
        for(unsigned sample=0;sample<=10;++sample){rvtts::Timing timing{};int e=rvtts::execute(method,inputs[method],output,&timing);if(e)return e;
            e=compare(output,expected[method],rvtts::output_sizes[method],tolerance[method],rvtts_method_error[method]);if(e)return e;
            if(sample)rvtts_method_timing[method][sample-1]=timing;
        }
        rvtts_method_weights[method]=ethos_benchmark.weights_size;rvtts_method_arena[method]=ethos_benchmark.arena_size;
        printf("Method %s error=%.9f tolerance=%.9f weights=%lu arena=%lu\n",rvtts::names[method],rvtts_method_error[method],tolerance[method],(unsigned long)ethos_benchmark.weights_size,(unsigned long)ethos_benchmark.arena_size);
        for(unsigned sample=0;sample<10;++sample){const auto &t=rvtts_method_timing[method][sample];printf("MethodSample %s %u ticks=%lu cycles=%llu invocations=%lu overflow=%lu\n",rvtts::names[method],sample+1,(unsigned long)t.ticks,(unsigned long long)t.cycles,(unsigned long)t.invocations,(unsigned long)t.overflow);}
    }
    const unsigned lengths[]={2,81,157};
    const uint8_t *phones[]={rvtts_sequence_2_phones,rvtts_sequence_81_phones,rvtts_sequence_157_phones};
    const uint8_t *stress[]={rvtts_sequence_2_stress,rvtts_sequence_81_stress,rvtts_sequence_157_stress};
    const uint16_t *words[]={rvtts_sequence_2_words,rvtts_sequence_81_words,rvtts_sequence_157_words};
    const float *references[]={rvtts_sequence_2_features,rvtts_sequence_81_features,rvtts_sequence_157_features};
    for(unsigned sequence=0;sequence<3;++sequence){rvtts_benchmark_stage=20+sequence;
        for(unsigned sample=0;sample<=10;++sample){rvtts::Timing timing{};int e=rvtts::features(phones[sequence],stress[sequence],words[sequence],lengths[sequence],output,&timing);if(e)return e;
            e=compare(output,references[sequence],lengths[sequence]*20,0.01f,rvtts_sequence_error[sequence]);if(e)return e;
            if(sample)rvtts_sequence_timing[sequence][sample-1]=timing;
        }
        printf("Sequence frames=%u audio_ms=%u max_error=%.9f tolerance=0.01\n",lengths[sequence],lengths[sequence]*10,rvtts_sequence_error[sequence]);
        for(unsigned sample=0;sample<10;++sample){const auto &t=rvtts_sequence_timing[sequence][sample];printf("SequenceSample %u %u ticks=%lu cycles=%llu invocations=%lu overflow=%lu\n",lengths[sequence],sample+1,(unsigned long)t.ticks,(unsigned long long)t.cycles,(unsigned long)t.invocations,(unsigned long)t.overflow);}
    }
    auto usage=arm::embedded::runtime_memory_usage();printf("Pools method=%lu temp=%lu reserved=%lu\n",(unsigned long)usage.method_peak,(unsigned long)usage.temp_peak,(unsigned long)(APP_METHOD_POOL_SIZE+APP_TEMP_POOL_SIZE));
    printf("Scope: method execute excludes input/output copy and validation; schedule includes input construction, recurrent state copies, tiling and finite checks; excludes vocoder, PCM, UART, LCD and playback.\n");
    rvtts_benchmark_complete=1;rvtts_benchmark_stage=30;printf("Test_result: PASS\n");fflush(stdout);return 0;
}
}
extern "C" int app_main(void) {
    rvtts_benchmark_stage=1;int error=rvtts::initialize();if(!error){rvtts_benchmark_stage=2;error=benchmark();}
    pipeline_error=error;if(error){printf("rvTTS benchmark failed error=%ld stage=%lu\n",(long)error,(unsigned long)rvtts_benchmark_stage);fflush(stdout);}return error;
}

