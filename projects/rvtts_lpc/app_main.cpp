// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "hardware_params.h"
#include "stream_init.hpp"
#include "rvtts_engine.hpp"
#include "model/model_test_data.h"
#include "cmsis_os2.h"
#include "main.h"
#include "board_nodes.h"
#include "dsp/lpcvoc_check.hpp"
#include <cmath>
#include <cstdio>
extern "C" {volatile int32_t pipeline_error;volatile uint32_t rvtts_selftest_pass;}
namespace {
alignas(32) float output[2048];
int selftest(){
    const float*inputs[]={rvtts_prosody_pre_input,rvtts_prosody_gru_input,rvtts_prosody_head_input,rvtts_acoustic_input};
    const float*expected[]={rvtts_prosody_pre_expected,rvtts_prosody_gru_expected,rvtts_prosody_head_expected,rvtts_acoustic_expected};
    const float tolerance[]={RVTTS_PROSODY_PRE_ATOL+1e-6f,RVTTS_PROSODY_GRU_ATOL+1e-6f,RVTTS_PROSODY_HEAD_ATOL+1e-6f,RVTTS_ACOUSTIC_ATOL+1e-6f};
    for(unsigned m=0;m<4;++m){int e=rvtts::execute(m,inputs[m],output);if(e)return e;for(unsigned i=0;i<rvtts::output_sizes[m];++i)if(!std::isfinite(output[i])||std::fabs(output[i]-expected[m][i])>tolerance[m])return -440;}
    rvtts_selftest_pass=1;return 0;
}
}
extern "C" int app_main(){
    HardwareParams hw{};int error=hardware_params_init(&hw);if(!error)error=selftest();
    if(!error)error=lpcvoc_optimization_check();
    if(!error)error=stream_configure_and_start(&hw);pipeline_error=error;
    if(error){printf("rvTTS startup failed error=%ld\r\n",(long)error);return error;}
    printf("rvTTS LPC self-test PASS. UART4 115200 8N1; enter a short English sentence and CR/LF. Maximum160 ASCII characters /224 frontend frames. I2S3 WM8904 stereo16kHz; connect powered speaker to Audio Out.\r\n");fflush(stdout);
    for(;;){osDelay(5000);if(stream_stats.input_editing)continue;printf("LIVE error=%ld requests=%lu acks=%lu played=%lu completed=%lu first_ticks=%lu irq=%lu interval_min=%lu interval_max=%lu lcd=%lu skips=%lu underruns=%lu deadlines=%lu drops=%lu uart_drop=%lu uart_error=%lu generated=%lu queue=%lu waits=%lu done=%lu hash=%08lx played_hash=%08lx starves=%lu\r\n",(long)pipeline_error,(unsigned long)stream_stats.requests,(unsigned long)stream_stats.acknowledgments,(unsigned long)stream_stats.played_blocks,(unsigned long)stream_stats.completed_blocks,(unsigned long)stream_stats.first_audio_ticks,(unsigned long)stream_stats.audio_callbacks,(unsigned long)stream_stats.audio_interval_min,(unsigned long)stream_stats.audio_interval_max,(unsigned long)stream_stats.lcd_frames,(unsigned long)stream_stats.lcd_skipped,(unsigned long)stream_stats.audio_underruns,(unsigned long)stream_stats.deadline_misses,(unsigned long)stream_stats.event_drops,(unsigned long)board_uart_rx_drops,(unsigned long)board_uart_rx_errors,(unsigned long)stream_stats.generated_blocks,(unsigned long)stream_stats.pcm_queue_max,(unsigned long)stream_stats.pcm_queue_waits,(unsigned long)stream_stats.generation_done_ticks,(unsigned long)stream_stats.pcm_hash,(unsigned long)stream_stats.played_hash,(unsigned long)stream_stats.playback_starvations);fflush(stdout);}
}
