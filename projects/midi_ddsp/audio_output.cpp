// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "audio_output.hpp"
#include "hardware_params.h"
#include "../../board/DevKit-E8/audio_output.h"
// Board selection stays outside portable synthesis and CMSIS Stream nodes.
extern "C" int midi_ddsp_audio_init(){return devkit_e8_audio_init();}
extern "C" int midi_ddsp_audio_write(const int16_t *mono,unsigned frames){return devkit_e8_audio_write(mono,frames);}
extern "C" int midi_ddsp_audio_drain(unsigned timeout_ms){return devkit_e8_audio_drain(timeout_ms);}
extern "C" void midi_ddsp_audio_stop(){devkit_e8_audio_stop();}
extern "C" int hardware_params_init(HardwareParams *params){
    if(!params)return -912;
    const int error=midi_ddsp_audio_init();
    if(!error)params->audio_write=midi_ddsp_audio_write;
    return error;
}
extern "C" void hardware_params_uninit(HardwareParams *){midi_ddsp_audio_stop();}
