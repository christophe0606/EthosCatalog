// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
// Define DETECTION_NETWORK_ONLY in the project to select the isolated harness.
#if defined(DETECTION_NETWORK_ONLY)
#include "model/network_benchmark.cpp"
#else
#define DETECTION_DEMO
#include "model/network_benchmark.cpp"
#define DETECTION_HOST_TEST
#include "nodes_impl/FaceDecode.hpp"
#undef DETECTION_HOST_TEST
#define CMSIS_STREAM_C_ONLY
#include "../../common/nodes_impl/c/Rgb888ToGrayInt8.hpp"
#undef CMSIS_STREAM_C_ONLY
#include "model/demo_test_data.h"
#include "hardware_params.h"
#include "stream_init.hpp"
#include <cmath>
extern "C" {
extern StreamStats stream_stats;
volatile uint32_t detection_preprocess_passed,detection_decode_passed,detection_demo_started;
void *detection_module(void) { return module; }
int8_t *detection_input(void) { return inference_input; }
int detection_forward(void *runtime,const int8_t *input,int8_t *head0,int8_t *head1,uint32_t *ticks)
{
    if(input!=inference_input) std::memcpy(inference_input,input,sizeof(inference_input));
    if(!runtime) return -833;
    return forward(head0,head1,ticks,static_cast<arm::embedded::EmbeddedModule*>(runtime));
}
}
static int test_processing()
{
    if(rgb888_to_gray_int8(detection_sample_rgb,192,192,192*3,inference_input)) return -830;
    if(std::memcmp(inference_input,detection_test_inputs[0],192*192)) return -830;
    detection_preprocess_passed=1;
    FaceBox workspace[FACE_MAX_CANDIDATES],boxes[FACE_MAX_CANDIDATES];
    unsigned n=face_decode(detection_outputs0[0],detection_outputs1[0],workspace,boxes);
    if(n!=2) return -831;
    const float expected[2][5]={{170.113007f,61.199524f,186.964111f,82.800476f,.917512051f},
        {3.601769f,33.478592f,32.663387f,72.863625f,.661385055f}};
    for(unsigned i=0;i<2;++i) {
        const float actual[5]={boxes[i].x0,boxes[i].y0,boxes[i].x1,boxes[i].y1,boxes[i].score};
        for(unsigned j=0;j<5;++j) if(std::fabs(actual[j]-expected[i][j])>(j==4?.00001f:.001f)) return -832;
    }
    detection_decode_passed=1;
    printf("Detection preprocessing and two-face decode self-test: PASS\n");
    return 0;
}
extern "C" int app_main(void)
{
    int error=test_network();
    if(!error) error=test_processing();
    detection_benchmark_complete=1;
    static HardwareParams hardware;
    if(!error) error=hardware_params_init(&hardware);
    pipeline_error=error; stream_stats.error=error; app_result=error;
    if(!error) error=stream_configure_and_start(&hardware);
    if(error) { pipeline_error=error; stream_stats.error=error; app_result=error; }
    if(error) printf("Detection demo startup: FAIL %d\n",error);
    else { detection_demo_started=1; printf("Detection live CMSIS Stream: started, 200 ms frame budget\n"); }
    fflush(stdout);
    for(;;) { ++detection_heartbeat; osDelay(1000); }
}
#endif
