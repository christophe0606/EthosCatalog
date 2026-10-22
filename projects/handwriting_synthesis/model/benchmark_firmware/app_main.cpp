// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>
#include "embedded_runtime.hpp"
#include "hardware_params.h"
#include "nodes_impl/handwriting_engine.hpp"
#include "model/model_test_data.h"
#include "model/cpu_test_data.h"
#include "cmsis_os2.h"
#include "main.h"
#include "se_services_port.h"
extern "C" {
volatile int32_t pipeline_error;
volatile uint32_t handwriting_test_passed, handwriting_benchmark_complete;
// Explicit measurement trigger: loading/attaching may reset the core more
// than once. Keep the NPU idle until the debug session has fully connected.
volatile uint32_t handwriting_benchmark_start;
uint64_t handwriting_benchmark_cycles[64];
uint32_t handwriting_benchmark_ticks[64];
}
namespace {
HardwareParams hardware;
alignas(handwriting::Engine) unsigned char engine_storage[sizeof(handwriting::Engine)];
float dense_output[1600];
const float *const inputs[5]={handwriting_lstm1_input,handwriting_attention_input,handwriting_lstm2_input,handwriting_lstm3_input,handwriting_gmm_input};
const float *const expected[5]={handwriting_lstm1_expected,handwriting_attention_expected,handwriting_lstm2_expected,handwriting_lstm3_expected,handwriting_gmm_expected};
const float tolerances[5]={HANDWRITING_LSTM1_BOARD_ATOL,HANDWRITING_ATTENTION_BOARD_ATOL,HANDWRITING_LSTM2_BOARD_ATOL,HANDWRITING_LSTM3_BOARD_ATOL,HANDWRITING_GMM_BOARD_ATOL};
int cpu_test() {
    float h[400],c[400]; std::memcpy(c,cpu_cell_c,sizeof(c));
    handwriting::Engine::cell(cpu_cell_gates,h,c);
    float max_error=0;
    for(unsigned i=0;i<400;++i) {
        max_error=std::max(max_error,std::fabs(h[i]-cpu_cell_h_expected[i]));
        max_error=std::max(max_error,std::fabs(c[i]-cpu_cell_c_expected[i]));
    }
    handwriting::State s{}; s.length=4;
    s.characters[0]=40; s.characters[1]=2; s.characters[2]=40; s.characters[3]=0;
    std::memcpy(s.kappa,cpu_window_kappa,sizeof(s.kappa));
    handwriting::Engine::window(cpu_window_attention,s);
    for(unsigned i=0;i<73;++i) max_error=std::max(max_error,std::fabs(s.window[i]-cpu_window_expected[i]));
    for(unsigned i=0;i<4;++i) max_error=std::max(max_error,std::fabs(s.phi[i]-cpu_phi_expected[i]));
    printf("CPU fixture: max_error=%.9f tolerance=0.000003\n",max_error);
    return max_error<=.000003f?0:-426;
}
bool pmu_ok(unsigned count) {
    return ethos_benchmark.invocations==count && ethos_benchmark.cycles &&
        !ethos_benchmark.overflow && ethos_benchmark.cycles==ethos_benchmark.active_cycles;
}
int benchmark(handwriting::Engine &engine) {
    uint32_t cpu_hz=0,axi_hz=0,se=0;
    if(SERVICES_clocks_setting_get(se_services_s_handle,CLOCK_SETTING_EXTSYS0_FREQ,&cpu_hz,&se)!=SERVICES_REQ_SUCCESS || se || !cpu_hz) return -410;
    if(SERVICES_clocks_setting_get(se_services_s_handle,CLOCK_SETTING_AXI_FREQ,&axi_hz,&se)!=SERVICES_REQ_SUCCESS || se || !axi_hz) return -410;
    uint32_t timer_hz=osKernelGetSysTimerFreq(); if(!timer_hz) return -410;
    printf("Handwriting Release benchmark: CPU=%lu AXI_NPU=%lu timer=%lu Hz\n",(unsigned long)cpu_hz,(unsigned long)axi_hz,(unsigned long)timer_hz);
    printf("Model=%lu bytes; engine=%u bytes; method_reserved=%u temp_reserved=%u\n",(unsigned long)hardware.model_size,(unsigned)sizeof(engine),(unsigned)hardware.method_pool_size,(unsigned)hardware.temp_pool_size);
    int error=cpu_test(); if(error) return error;
    for(unsigned m=0;m<5;++m) {
        float max_error=0;
        for(unsigned sample=0;sample<=10;++sample) {
            ethos_benchmark_reset(); uint32_t ticks=0;
            error=engine.dense(m,inputs[m],dense_output,&ticks); if(error) return error;
            if(!pmu_ok(1)) return -427;
            for(unsigned i=0;i<handwriting::output_sizes[m];++i) {
                float delta=std::fabs(dense_output[i]-expected[m][i]);
                max_error=std::max(max_error,delta);
                if(delta>tolerances[m]) { printf("Mismatch %s[%u]: actual=%.9f expected=%.9f tolerance=%.9f\n",handwriting::method_names[m],i,dense_output[i],expected[m][i],tolerances[m]); return -428; }
            }
            if(sample) printf("DENSE %s sample=%u cycles=%llu ticks=%lu invocations=%lu overflow=%lu\n",handwriting::method_names[m],sample,(unsigned long long)ethos_benchmark.cycles,(unsigned long)ticks,(unsigned long)ethos_benchmark.invocations,(unsigned long)ethos_benchmark.overflow);
        }
        printf("REGION %s weights=%lu weights_address=0x%08lx arena=%lu arena_address=0x%08lx max_error=%.9f tolerance=%.9f\n",handwriting::method_names[m],(unsigned long)ethos_benchmark.weights_size,(unsigned long)ethos_benchmark.weights_address,(unsigned long)ethos_benchmark.arena_size,(unsigned long)ethos_benchmark.arena_address,max_error,tolerances[m]);
        fflush(stdout);
    }
    const char *sentence="The quick brown fox writes white words on black. This sentence fills a LCD.";
    static_assert(sizeof("The quick brown fox writes white words on black. This sentence fills a LCD.")-1==75);
    error=engine.begin(sentence,42); if(error) return error;
    handwriting::Stroke point;
    for(unsigned sample=0;sample<=64;++sample) {
        ethos_benchmark_reset(); uint32_t start=osKernelGetSysTimerCount();
        error=engine.step(point);
        uint32_t ticks=osKernelGetSysTimerCount()-start;
        if(error) return error<0?error:-429;
        if(!pmu_ok(5) || !engine.finite()) return -430;
        if(sample) {
            handwriting_benchmark_cycles[sample-1]=ethos_benchmark.cycles;
            handwriting_benchmark_ticks[sample-1]=ticks;
            printf("STEP sample=%u cycles=%llu ticks=%lu invocations=%lu overflow=%lu point=%.6f,%.6f,%.0f\n",sample,(unsigned long long)ethos_benchmark.cycles,(unsigned long)ticks,(unsigned long)ethos_benchmark.invocations,(unsigned long)ethos_benchmark.overflow,point.dx,point.dy,point.pen_up);
        }
    }
    auto usage=arm::embedded::runtime_memory_usage();
    printf("POOLS method_peak=%u temp_peak=%u\n",(unsigned)usage.method_peak,(unsigned)usage.temp_peak);
    handwriting_test_passed=1; handwriting_benchmark_complete=1;
    printf("Test_result: PASS\n"); fflush(stdout); return 0;
}
}
extern "C" int app_main(void) {
    while(!handwriting_benchmark_start) osDelay(10);
    int error=hardware_params_init(&hardware);
    if(!error) {
        auto *engine=new(engine_storage) handwriting::Engine(static_cast<arm::embedded::EmbeddedModule*>(hardware.module));
        error=benchmark(*engine);
    }
    pipeline_error=error; stream_stats.error=error;
    if(error) { printf("Handwriting benchmark FAILED: %d\n",error); fflush(stdout); }
    return error;
}
