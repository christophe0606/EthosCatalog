// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "hardware_params.h"
#include "board_nodes.h"
#include "model_pte.h"
#include "rvtts_engine.hpp"
#include "nodes_impl/fargan_cpu.h"
extern "C" {
#include "../rvtts_lpc/upstream/pron_dict.h"
#include "../rvtts_lpc/upstream/word_vocab_packed.h"
int rvtts_audio_init(void);
int rvtts_audio_write(const int16_t*,unsigned);
StreamStats stream_stats;
}
int hardware_params_init(HardwareParams*p){
    if(!p)return -400;int error=rvtts::initialize();if(error)return error;
    error=rvtts_fargan_cpu_bind(reinterpret_cast<const void*>(MODEL_ASSET_FARGAN_CPU_ADDRESS),MODEL_ASSET_FARGAN_CPU_SIZE_BYTES);if(error)return error;
    if(pron_dict_init(reinterpret_cast<const void*>(MODEL_ASSET_DICTIONARY_ADDRESS),MODEL_ASSET_DICTIONARY_SIZE_BYTES)||word_vocab_init(reinterpret_cast<const void*>(MODEL_ASSET_WORDS_ADDRESS),MODEL_ASSET_WORDS_SIZE_BYTES))return -450;
    if(board_uart_rx_start())return -451;error=rvtts_audio_init();if(error)return error;
    *p={&stream_stats,board_uart_rx_read,rvtts_audio_write,lcd_init_display,lcd_wait_for_release,lcd_submit_region};return 0;
}
