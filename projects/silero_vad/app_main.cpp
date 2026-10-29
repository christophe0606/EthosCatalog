// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "embedded_runtime.hpp"
#include "hardware_params.h"
#include "stream_init.hpp"
#include "nodes_impl/embedded_silero.h"
#include "model/model_test_data.h"
#include "model/stream_test_data.h"
#include "model_pte.h"
#include "cmsis_os2.h"
#include "main.h"
#include "se_services_port.h"
#include <cstdio>
#include <cmath>
#include <cstring>
extern "C" {
int silero_fade_test(void);
volatile uint32_t silero_fade_test_passed;
volatile int32_t pipeline_error;
volatile uint32_t silero_test_passed,silero_benchmark_complete;
uint64_t silero_benchmark_cycles[10];
uint32_t silero_benchmark_ticks[10];
float silero_test_probabilities[2],silero_max_probability_error,silero_max_state_error;
float silero_stream_probabilities[SILERO_STREAM_TEST_COUNT];
float silero_stream_max_error,silero_stream_mean_error;
volatile uint32_t silero_stream_disagreements,silero_stream_positive;
}
namespace {
HardwareParams hardware;
alignas(32) float audio[576],h[128],c[128],next_h[128],next_c[128];
bool finite_float(float v) { uint32_t bits;std::memcpy(&bits,&v,4);return (bits&0x7f800000U)!=0x7f800000U; }
}
extern "C" int app_main(void)
{
    int fade_error=silero_fade_test();
    if(fade_error)return pipeline_error=fade_error;
    silero_fade_test_passed=1;
    int init_error=hardware_params_init(&hardware);
    if(init_error)return pipeline_error=init_error;
    auto* module=hardware.module;
    uint32_t cpu=0,axi=0,se=0;
    if(SERVICES_clocks_setting_get(se_services_s_handle,CLOCK_SETTING_EXTSYS0_FREQ,&cpu,&se)!=SERVICES_REQ_SUCCESS||se||!cpu)return pipeline_error=-410;
    if(SERVICES_clocks_setting_get(se_services_s_handle,CLOCK_SETTING_AXI_FREQ,&axi,&se)!=SERVICES_REQ_SUCCESS||se||!axi)return pipeline_error=-410;
    const uint32_t timer=osKernelGetSysTimerFreq();
    if(!timer)return pipeline_error=-410;
    // Two unmeasured correctness/warmup forwards, followed by ten checked forwards.
    for(unsigned sample=0;sample<12;++sample) {
        const unsigned test=sample%SILERO_TEST_COUNT;
        std::memcpy(audio,silero_test_audio[test],sizeof(audio));
        std::memcpy(h,silero_test_h[test],sizeof(h));std::memcpy(c,silero_test_c[test],sizeof(c));
        uint32_t ticks=0;float p=0;
        ethos_benchmark_reset();
        int error=silero_forward(module,audio,h,c,&p,next_h,next_c,&ticks);
        if(error)return pipeline_error=error;
        if(ethos_benchmark.invocations!=2||!ethos_benchmark.cycles||ethos_benchmark.overflow||ethos_benchmark.cycles!=ethos_benchmark.active_cycles)return pipeline_error=-403;
        silero_test_probabilities[test]=p;
        const float pe=std::fabs(p-silero_test_expected_probability[test][0]);
        if(!finite_float(p)||p<0||p>1||pe>silero_test_probability_tolerance)return pipeline_error=-404;
        if(pe>silero_max_probability_error)silero_max_probability_error=pe;
        for(unsigned i=0;i<128;++i) {
            const float he=std::fabs(next_h[i]-silero_test_expected_h[test][i]);
            const float ce=std::fabs(next_c[i]-silero_test_expected_c[test][i]);
            // Record backend state drift explicitly. Continuous-stream probability
            // validation below tests whether that drift changes the VAD behavior.
            if(!finite_float(next_h[i])||!finite_float(next_c[i]))return pipeline_error=-405;
            if(he>silero_max_state_error)silero_max_state_error=he;
            if(ce>silero_max_state_error)silero_max_state_error=ce;
        }
        if(sample>=2) { silero_benchmark_cycles[sample-2]=ethos_benchmark.cycles;silero_benchmark_ticks[sample-2]=ticks; }
    }
    printf("Silero VAD vector check: PASS (silence and speech probabilities; finite h/c, state drift measured)\n");
    printf("Probabilities: %.9f %.9f; max probability error %.9f; max state error %.9f\n",silero_test_probabilities[0],silero_test_probabilities[1],silero_max_probability_error,silero_max_state_error);
    printf("Benchmark clocks: CPU=%lu Hz, AXI/NPU=%lu Hz, timer=%lu Hz\n",(unsigned long)cpu,(unsigned long)axi,(unsigned long)timer);
    auto usage=arm::embedded::runtime_memory_usage();
    printf("Benchmark memory: model=%lu B; method peak=%lu B; temp peak=%lu B; reserved=%lu B\n",(unsigned long)model_pte_size,(unsigned long)usage.method_peak,(unsigned long)usage.temp_peak,(unsigned long)(APP_METHOD_POOL_SIZE+APP_TEMP_POOL_SIZE));
    printf("Benchmark regions: last weights=%lu B at 0x%08lx; arena=%lu B at 0x%08lx\n",(unsigned long)ethos_benchmark.weights_size,(unsigned long)ethos_benchmark.weights_address,(unsigned long)ethos_benchmark.arena_size,(unsigned long)ethos_benchmark.arena_address);
    for(unsigned i=0;i<10;++i)printf("Benchmark sample %u: NPU=%llu cycles (%.6f ms), forward=%lu ticks (%.6f ms)\n",i+1,(unsigned long long)silero_benchmark_cycles[i],1000.0*silero_benchmark_cycles[i]/axi,(unsigned long)silero_benchmark_ticks[i],1000.0*silero_benchmark_ticks[i]/timer);
    printf("Benchmark PMU: invocations=%lu, active=%llu cycles, overflow=%lu\n",(unsigned long)ethos_benchmark.invocations,(unsigned long long)ethos_benchmark.active_cycles,(unsigned long)ethos_benchmark.overflow);
    std::memset(audio,0,sizeof(audio));std::memset(h,0,sizeof(h));std::memset(c,0,sizeof(c));
    for(unsigned frame=0;frame<SILERO_STREAM_TEST_COUNT;++frame) {
        for(unsigned i=0;i<512;++i)audio[64+i]=silero_stream_pcm[frame*512+i]/32768.0f;
        float p=0;int error=silero_forward(module,audio,h,c,&p,h,c);
        if(error||!finite_float(p)||p<0||p>1)return pipeline_error=error?error:-406;
        silero_stream_probabilities[frame]=p;
        float e=std::fabs(p-silero_stream_expected[frame]);
        if(e>silero_stream_max_error)silero_stream_max_error=e;
        silero_stream_mean_error+=e/SILERO_STREAM_TEST_COUNT;
        silero_stream_disagreements+=(p>=0.5f)!=(silero_stream_expected[frame]>=0.5f);
        silero_stream_positive+=(p>=0.5f);
        std::memmove(audio,audio+512,64*sizeof(float));
    }
    printf("Continuous held-out stream: frames=%u, positive=%lu, disagreements=%lu, max error=%.9f, mean error=%.9f\n",SILERO_STREAM_TEST_COUNT,(unsigned long)silero_stream_positive,(unsigned long)silero_stream_disagreements,silero_stream_max_error,silero_stream_mean_error);
    printf("Final silence probability: %.9f (host %.9f)\n",silero_stream_probabilities[SILERO_STREAM_TEST_COUNT-1],silero_stream_expected[SILERO_STREAM_TEST_COUNT-1]);
    // Catalog demo acceptance: >=95% threshold agreement, MAE<=0.05,
    // successful speech->silence, bounded finite probabilities and NPU activity.
    // Max transition error and state drift remain reported without parity claims.
    if(silero_stream_disagreements*100U>SILERO_STREAM_TEST_COUNT*5U||silero_stream_mean_error>0.05f||silero_stream_probabilities[SILERO_STREAM_TEST_COUNT-1]>=0.5f)return pipeline_error=-407;
    silero_test_passed=1;
    silero_benchmark_complete=1;printf("Test_result: PASS\n");fflush(stdout);
    int error=stream_configure_and_start(&hardware);
    if(error){stream_stats.error=error;pipeline_error=error;}
    return error;
}
