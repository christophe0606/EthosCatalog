// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "embedded_runtime.hpp"
#include "nodes_impl/embedded_rnnoise.h"
#include "model/model_test_data.h"
#include "model/sequence_test_data.h"
#include "model_pte.h"
#include "main.h"
#include "cmsis_os2.h"
#include "se_services_port.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
extern "C" {
volatile int32_t pipeline_error;
volatile uint32_t rnnoise_test_passed,rnnoise_parity_passed,rnnoise_benchmark_complete,rnnoise_numerical_passed;
uint64_t rnnoise_cycles[10];uint32_t rnnoise_ticks[10];
uint32_t rnnoise_fixture_max[5],rnnoise_sequence_max[5];
uint32_t rnnoise_fixture_source_max[5],rnnoise_sequence_source_max[5];
float rnnoise_sequence_mae_lsb[5],rnnoise_sequence_source_mae_lsb[5];
uint32_t rnnoise_clock_hz[3],rnnoise_pool_peaks[2];
float rnnoise_gain_mae,rnnoise_vad_mae,rnnoise_source_gain_mae,rnnoise_source_vad_mae;
}
namespace {
alignas(32) int8_t features[42],outputs[191];
alignas(32) RNNoiseState state;
const unsigned offsets[]{0,96,118,166,190,191};
}
extern "C" int rnnoise_startup_test(void *module)
{
    uint32_t cpu=0,axi=0,se=0;
    if(SERVICES_clocks_setting_get(se_services_s_handle,CLOCK_SETTING_EXTSYS0_FREQ,&cpu,&se)!=SERVICES_REQ_SUCCESS||se||!cpu)return pipeline_error=-411;
    if(SERVICES_clocks_setting_get(se_services_s_handle,CLOCK_SETTING_AXI_FREQ,&axi,&se)!=SERVICES_REQ_SUCCESS||se||!axi)return pipeline_error=-411;
    const uint32_t timer=osKernelGetSysTimerFreq();if(!timer)return pipeline_error=-411;
    rnnoise_clock_hz[0]=cpu;rnnoise_clock_hz[1]=axi;rnnoise_clock_hz[2]=timer;
    const int8_t *source[]{rnnoise_source_test_output_0,rnnoise_source_test_output_1,rnnoise_source_test_output_2,rnnoise_source_test_output_3,rnnoise_source_test_output_4};
    const int8_t *expected[]{rnnoise_test_output_0,rnnoise_test_output_1,rnnoise_test_output_2,rnnoise_test_output_3,rnnoise_test_output_4};
    for(unsigned sample=0;sample<12;++sample) {
        std::memcpy(features,rnnoise_test_input_0,42);std::memcpy(state.vad,rnnoise_test_input_1,24);std::memcpy(state.noise,rnnoise_test_input_2,48);std::memcpy(state.denoise,rnnoise_test_input_3,96);
        ethos_benchmark_reset();uint32_t ticks=0;int e=rnnoise_forward(module,features,state,outputs,&ticks);if(e)return pipeline_error=e;
        if(ethos_benchmark.invocations!=2||!ethos_benchmark.cycles||ethos_benchmark.overflow||ethos_benchmark.cycles!=ethos_benchmark.active_cycles)return pipeline_error=-412;
        for(unsigned o=0;o<5;++o)for(unsigned j=offsets[o];j<offsets[o+1];++j) {
            unsigned e=std::abs(int(outputs[j])-int(expected[o][j-offsets[o]]));if(e>rnnoise_fixture_max[o])rnnoise_fixture_max[o]=e;
            unsigned se=std::abs(int(outputs[j])-int(source[o][j-offsets[o]]));if(se>rnnoise_fixture_source_max[o])rnnoise_fixture_source_max[o]=se;
        }
        if(sample>=2){rnnoise_cycles[sample-2]=ethos_benchmark.cycles;rnnoise_ticks[sample-2]=ticks;}
    }
    printf("RNNoise clocks: CPU=%lu AXI/NPU=%lu timer=%lu Hz\n",(unsigned long)cpu,(unsigned long)axi,(unsigned long)timer);
    for(unsigned i=0;i<10;++i)printf("Benchmark sample %u: NPU=%llu cycles (%.6f ms), forward=%lu ticks (%.6f ms)\n",i+1,(unsigned long long)rnnoise_cycles[i],1000.0*rnnoise_cycles[i]/axi,(unsigned long)rnnoise_ticks[i],1000.0*rnnoise_ticks[i]/timer);
    printf("Fixture max LSB: %lu %lu %lu %lu %lu (provisional tolerance %u)\n",(unsigned long)rnnoise_fixture_max[0],(unsigned long)rnnoise_fixture_max[1],(unsigned long)rnnoise_fixture_max[2],(unsigned long)rnnoise_fixture_max[3],(unsigned long)rnnoise_fixture_max[4],RNNOISE_TEST_OUTPUT_TOLERANCE_LSB);
    rnnoise_reset(state);
    for(unsigned f=0;f<RNNOISE_SEQUENCE_FRAMES;++f) {
        std::memcpy(features,rnnoise_sequence_features[f],42);int e=rnnoise_forward(module,features,state,outputs);if(e)return pipeline_error=e;
        for(unsigned o=0;o<5;++o)for(unsigned j=offsets[o];j<offsets[o+1];++j){unsigned e=std::abs(int(outputs[j])-int(rnnoise_sequence_host_outputs[f][j]));if(e>rnnoise_sequence_max[o])rnnoise_sequence_max[o]=e;
            unsigned se=std::abs(int(outputs[j])-int(rnnoise_sequence_source_outputs[f][j]));if(se>rnnoise_sequence_source_max[o])rnnoise_sequence_source_max[o]=se;
            rnnoise_sequence_mae_lsb[o]+=e/float((offsets[o+1]-offsets[o])*RNNOISE_SEQUENCE_FRAMES);rnnoise_sequence_source_mae_lsb[o]+=se/float((offsets[o+1]-offsets[o])*RNNOISE_SEQUENCE_FRAMES);}
        for(unsigned j=96;j<118;++j){rnnoise_gain_mae+=std::abs(int(outputs[j])-int(rnnoise_sequence_host_outputs[f][j]))/(256.0f*22*RNNOISE_SEQUENCE_FRAMES);rnnoise_source_gain_mae+=std::abs(int(outputs[j])-int(rnnoise_sequence_source_outputs[f][j]))/(256.0f*22*RNNOISE_SEQUENCE_FRAMES);}
        rnnoise_vad_mae+=std::abs(int(outputs[190])-int(rnnoise_sequence_host_outputs[f][190]))/(256.0f*RNNOISE_SEQUENCE_FRAMES);rnnoise_source_vad_mae+=std::abs(int(outputs[190])-int(rnnoise_sequence_source_outputs[f][190]))/(256.0f*RNNOISE_SEQUENCE_FRAMES);
    }
    printf("Sequence160 max LSB: %lu %lu %lu %lu %lu\n",(unsigned long)rnnoise_sequence_max[0],(unsigned long)rnnoise_sequence_max[1],(unsigned long)rnnoise_sequence_max[2],(unsigned long)rnnoise_sequence_max[3],(unsigned long)rnnoise_sequence_max[4]);
    printf("Sequence MAE host gains=%.9f VAD=%.9f; source gains=%.9f VAD=%.9f\n",rnnoise_gain_mae,rnnoise_vad_mae,rnnoise_source_gain_mae,rnnoise_source_vad_mae);
    auto usage=arm::embedded::runtime_memory_usage();
    rnnoise_pool_peaks[0]=usage.method_peak;rnnoise_pool_peaks[1]=usage.temp_peak;
    printf("Memory: model=%lu method_peak=%lu temp_peak=%lu reserved=%lu B\n",(unsigned long)model_pte_size,(unsigned long)usage.method_peak,(unsigned long)usage.temp_peak,(unsigned long)(APP_METHOD_POOL_SIZE+APP_TEMP_POOL_SIZE));
    printf("Last delegate: weights=%lu@0x%08lx arena=%lu@0x%08lx\n",(unsigned long)ethos_benchmark.weights_size,(unsigned long)ethos_benchmark.weights_address,(unsigned long)ethos_benchmark.arena_size,(unsigned long)ethos_benchmark.arena_address);
    printf("PMU: invocations=%lu cycles=%llu active=%llu overflow=%lu\n",(unsigned long)ethos_benchmark.invocations,(unsigned long long)ethos_benchmark.cycles,(unsigned long long)ethos_benchmark.active_cycles,(unsigned long)ethos_benchmark.overflow);
    rnnoise_parity_passed=1;for(unsigned i=0;i<5;++i)if(rnnoise_fixture_max[i]>RNNOISE_TEST_OUTPUT_TOLERANCE_LSB)rnnoise_parity_passed=0;
    // Predeclared demo smoke gate; exact fixture parity remains a separate flag.
    rnnoise_numerical_passed=(rnnoise_source_gain_mae<=0.05f&&rnnoise_source_vad_mae<=0.03f);
    rnnoise_test_passed=1;rnnoise_benchmark_complete=1;printf("Runtime_test: PASS; Fixture_parity: %s\n",rnnoise_parity_passed?"PASS":"FAIL");fflush(stdout);
    return 0;
}
