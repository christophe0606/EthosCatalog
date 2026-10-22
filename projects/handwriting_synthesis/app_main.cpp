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
#include "stream_init.hpp"
#if defined(HANDWRITING_TRANSFER_BENCHMARK)
int handwriting_transfer_benchmark(handwriting::Engine &engine);
#endif
extern "C" {
volatile int32_t pipeline_error;
volatile uint32_t handwriting_test_passed, handwriting_benchmark_complete;
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
int self_test(handwriting::Engine &engine) {
    printf("Model=%lu bytes; engine=%u bytes; method_reserved=%u temp_reserved=%u\n",(unsigned long)hardware.model_size,(unsigned)sizeof(engine),(unsigned)hardware.method_pool_size,(unsigned)hardware.temp_pool_size);
    int error=cpu_test(); if(error) return error;
    for(unsigned m=0;m<5;++m) {
        ethos_benchmark_reset();
        error=engine.dense(m,inputs[m],dense_output); if(error) return error;
        if(!pmu_ok(1)) return -427;
        float max_error=0;
        for(unsigned i=0;i<handwriting::output_sizes[m];++i) {
            float delta=std::fabs(dense_output[i]-expected[m][i]); max_error=std::max(max_error,delta);
            if(delta>tolerances[m]) return -428;
        }
        printf("Startup %s PASS max_error=%.9f NPU=%lu\n",handwriting::method_names[m],max_error,(unsigned long)ethos_benchmark.invocations);
        printf("REGION %s weights=%lu weights_address=0x%08lx arena=%lu arena_address=0x%08lx max_error=%.9f tolerance=%.9f\n",handwriting::method_names[m],(unsigned long)ethos_benchmark.weights_size,(unsigned long)ethos_benchmark.weights_address,(unsigned long)ethos_benchmark.arena_size,(unsigned long)ethos_benchmark.arena_address,max_error,tolerances[m]);
    }
    auto usage=arm::embedded::runtime_memory_usage();
    printf("POOLS method_peak=%u temp_peak=%u\n",(unsigned)usage.method_peak,(unsigned)usage.temp_peak);
#if defined(HANDWRITING_TRANSFER_BENCHMARK)
    error=handwriting_transfer_benchmark(engine); if(error) return error;
#endif
    handwriting_test_passed=1; return 0;
}
}
extern "C" int app_main(void) {
    // Autonomous startup. The terminal is ready only after all board/model tests.
    int error=hardware_params_init(&hardware);
    if(!error) {
        auto *engine=new(engine_storage) handwriting::Engine(static_cast<arm::embedded::EmbeddedModule*>(hardware.module));
        error=self_test(*engine);
        engine->~Engine();
    }
    if(!error) error=stream_configure_and_start(&hardware);
    pipeline_error=error; stream_stats.error=error;
    if(error) printf("Handwriting startup FAILED: %d\n",error);
    else printf("Handwriting READY. UART4 115200 8N1; 1..75 characters, Return starts. White on black, 800x480.\n");
    fflush(stdout);
    return error;
}
