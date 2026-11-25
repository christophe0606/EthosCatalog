// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
// Deterministic, single-core interrupt interleaving of the production queue.
#include "audio_mock.h"
#include "audio_output.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
namespace {
void (*irq)(uint32_t)=nullptr;
const int32_t *pending=nullptr;
unsigned pending_items=0,tokens=0,ticks=1;
bool consume_on_wait=true;
uint16_t regs[64];
std::vector<int16_t> played;
int32_t send(const void *p,uint32_t n){assert(!pending);pending=static_cast<const int32_t*>(p);pending_items=n;return 0;}
void complete(){
    assert(pending&&pending_items==512);
    for(unsigned i=0;i<256;++i){
        assert(pending[i*2]==pending[i*2+1]);
        assert((uint32_t(pending[i*2])&65535u)==0);
        played.push_back(int16_t(pending[i*2]/65536));
    }
    pending=nullptr;ticks+=16;irq(ARM_SAI_EVENT_SEND_COMPLETE);
}
int32_t initialize(void (*fn)(uint32_t)){irq=fn;return 0;}
int32_t success(){return 0;}
int32_t power(int){return 0;}
int32_t volume(uint8_t percent){
    assert(percent==78); // Code49: +6 dB from43, +12 dB from original37.
    regs[0x39]=regs[0x3a]=uint16_t(unsigned(percent)*63/100);return 0;
}
int32_t control(uint32_t what,uint32_t a,uint32_t){
    if(what==ARM_SAI_ABORT_SEND||(what==ARM_SAI_CONTROL_TX&&!a))pending=nullptr;
    return 0;
}
void write_block(unsigned serial){
    int16_t block[256];
    for(unsigned i=0;i<256;++i)block[i]=int16_t((serial*256+i)%65536-32768);
    assert(devkit_e8_audio_write(block,256)==0);
}
void check_block(unsigned offset,unsigned serial){
    for(unsigned i=0;i<256;++i)assert(played[offset+i]==int16_t((serial*256+i)%65536-32768));
}
}
extern "C" {
ARM_DRIVER_SAI Driver_SAI3{initialize,success,power,control,send};
ARM_DRIVER_WM8904 WM8904{success,success,power,success,success,volume};
WM8904_CODEC_SLAVE_I2C_CONFIG i2c_cnfg{};
unsigned se_services_s_handle=0;
}
osSemaphoreId_t osSemaphoreNew(uint32_t maximum,uint32_t initial,const void*){assert(maximum==6);tokens=initial;return &tokens;}
int osSemaphoreDelete(osSemaphoreId_t){return osOK;}
int osSemaphoreAcquire(osSemaphoreId_t,uint32_t){
    if(!tokens&&consume_on_wait&&pending)complete();
    if(!tokens)return -1;
    --tokens;return osOK;
}
int osSemaphoreRelease(osSemaphoreId_t){assert(tokens<6);++tokens;return osOK;}
uint32_t osKernelGetTickFreq(){return 1000;}
uint32_t osKernelGetSysTimerFreq(){return 1000;}
uint32_t osKernelGetTickCount(){return ticks;}
uint32_t osKernelGetSysTimerCount(){return ticks;}
int osDelay(uint32_t t){if(consume_on_wait&&pending)complete();else ticks+=t;return osOK;}
int SERVICES_clocks_enable_clock(unsigned,unsigned,bool,uint32_t *error){*error=0;return 0;}
int pinconf_set(unsigned,unsigned,unsigned,unsigned){return 0;}
int WM8904_codec_i2c_write(WM8904_CODEC_SLAVE_I2C_CONFIG*,uint16_t reg,uint16_t value){regs[reg]=value;return 0;}
int WM8904_codec_i2c_read(WM8904_CODEC_SLAVE_I2C_CONFIG*,uint16_t reg,uint16_t *value){*value=regs[reg];return 0;}
int main(){
    assert(devkit_e8_audio_write(nullptr,256)==-912);
    assert(devkit_e8_audio_init()==0);
    assert(devkit_e8_audio_stats.codec_left_volume_code==49);
    assert(devkit_e8_audio_stats.codec_right_volume_code==49);
    assert(devkit_e8_audio_write(nullptr,256)==-912);
    int16_t block[256]{};assert(devkit_e8_audio_write(block,255)==-912);
    write_block(0);assert(!pending);write_block(1);assert(pending);
    const auto* playing = pending;
    assert(devkit_e8_audio_set_muted(true) == 0);
    assert(regs[0x39] == 0x131 && regs[0x3a] == 0x1b1);
    assert(pending == playing && devkit_e8_audio_stats.queued_blocks == 2);
    assert(devkit_e8_audio_set_muted(false) == 0);
    assert(regs[0x39] == 0x31 && regs[0x3a] == 0xb1);
    assert(pending == playing && devkit_e8_audio_stats.queued_blocks == 2);
    for(unsigned s=2;s<40;++s)write_block(s);
    assert(devkit_e8_audio_stats.queue_high_water==6);
    assert(devkit_e8_audio_drain(1000)==0);
    assert(devkit_e8_audio_stats.completed_blocks==40);
    assert(devkit_e8_audio_stats.queued_blocks==40);
    assert(devkit_e8_audio_stats.starvation_events==0);
    for(unsigned s=0;s<40;++s)check_block(s*256,s);
    assert(devkit_e8_audio_stats.deadline_misses==0);
    devkit_e8_audio_stop();assert(!pending);
    // Stop/reinitialize clears ownership, counters and partially queued data.
    played.clear();assert(devkit_e8_audio_init()==0);write_block(50);
    assert(devkit_e8_audio_drain(1000)==0);check_block(0,50);
    assert(devkit_e8_audio_stats.completed_blocks==1);
    assert(devkit_e8_audio_stats.starvation_events==0);devkit_e8_audio_stop();
    // A producer gap outputs zeros, counts one gap, then resumes in order.
    played.clear();assert(devkit_e8_audio_init()==0);write_block(60);write_block(61);
    complete();complete();assert(devkit_e8_audio_stats.starvation_events==1);
    complete();write_block(62);complete();complete();
    check_block(0,60);check_block(256,61);check_block(1024,62);
    assert(std::all_of(played.begin()+512,played.begin()+1024,[](int16_t v){return v==0;}));
    devkit_e8_audio_stop();
    // A stalled consumer reports a bounded wait failure without overwriting.
    assert(devkit_e8_audio_init()==0);consume_on_wait=false;
    for(unsigned s=0;s<6;++s)write_block(s);
    assert(devkit_e8_audio_write(block,256)==-913);
    assert(devkit_e8_audio_stats.write_timeouts==1);
    assert(devkit_e8_audio_stats.queued_blocks==6);devkit_e8_audio_stop();
    std::puts("PASS: production audio queue ordering, bounds, startup, drain, gap, reset, timeout");
}
