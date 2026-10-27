// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "app_params.h"
#include "WaveformDisplay.hpp"
#include "TextFrontend.hpp"
alignas(32) uint16_t WaveformDisplay::buffers_[2][480*800] __attribute__((section(".bss.lcd_frame_buf")));
alignas(32) rvtts::Work TextFrontend::work_ __attribute__((section(".bss.ai_pool")));
cg_status WaveformDisplay::init(){std::memset(buffers_,0,sizeof(buffers_));return hw_.lcd_init(buffers_[0])?CG_INIT_FAILURE:CG_SUCCESS;}
cg_status WaveformDisplay::processEvent(int port,Event&& e){
    if(port||e.event_id!=selectors[selWave]||!e.wellFormed<uint64_t>())return CG_BUFFER_ERROR;
    auto*s=reinterpret_cast<rvtts::Waveform*>(uintptr_t(e.get<uint64_t>()));uint32_t start=osKernelGetSysTimerCount();
    int error=hw_.lcd_acquire();if(error){s->pending.store(false,std::memory_order_release);return static_cast<cg_status>(error);}
    auto*b=buffers_[back_];std::fill(b,b+480*800,uint16_t(0x0843));
    for(int x=30;x<770;++x){pixel(b,x,240,0x39e7);pixel(b,x,60,0x39e7);pixel(b,x,420,0x39e7);}
    for(int x=30;x<770;x+=74)for(int y=60;y<=420;++y)pixel(b,x,y,0x18e4);
    int previous=240;
    for(int x=30;x<770;++x){int y=240-int(s->samples[(x-30)*160/740])*160/32768;
        for(int z=std::min(y,previous);z<=std::max(y,previous);++z)pixel(b,x,z,0x07ff);previous=y;}
    unsigned progress=s->total?std::min(740U,(s->position+1)*740/s->total):0;
    for(unsigned x=0;x<progress;++x)for(unsigned y=442;y<454;++y)pixel(b,30+x,y,0x07e0);
    error=hw_.lcd_present(b,0,0,480,800);if(!error){back_^=1;++hw_.stats->lcd_frames;}
    uint32_t elapsed=osKernelGetSysTimerCount()-start;if(elapsed>hw_.stats->lcd_ticks_max)hw_.stats->lcd_ticks_max=elapsed;
    s->pending.store(false,std::memory_order_release);return error?static_cast<cg_status>(error):CG_SUCCESS;
}
