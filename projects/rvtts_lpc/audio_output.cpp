// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "hardware_params.h"
#include "cmsis_os2.h"
#include "Driver_SAI.h"
#include "WM8904_driver.h"
#include "WM8904_codec_i2c.h"
#include "board_defs.h"
#include "pinconf.h"
#include "se_services_port.h"
#include "RTE_Components.h"
#include CMSIS_device_header
#include <atomic>
extern "C" ARM_DRIVER_SAI Driver_SAI3;
extern "C" ARM_DRIVER_WM8904 WM8904;
extern "C" WM8904_CODEC_SLAVE_I2C_CONFIG i2c_cnfg;
namespace {
constexpr unsigned Slots=2,Samples=320;
alignas(32) int32_t ring[Slots][Samples],silence[Samples];
uint32_t generations[Slots];
std::atomic<uint32_t> produced{0},consumed{0};
osSemaphoreId_t available;
bool started=false,current_ring=false;
uint32_t current_generation=0,last_irq=0;
void arm_next(){
    uint32_t read=consumed.load(std::memory_order_relaxed),write=produced.load(std::memory_order_acquire);
    current_ring=read!=write;const int32_t*data=silence;
    if(current_ring){data=ring[read%Slots];uint32_t generation=generations[read%Slots];
        if(generation&&generation!=current_generation)stream_stats.first_audio_ticks=osKernelGetSysTimerCount()-stream_stats.utterance_started;
        current_generation=generation;}
    else {if(current_generation)++stream_stats.audio_underruns;current_generation=0;}
    int error=Driver_SAI3.Send(data,Samples);if(error)stream_stats.error=-510+error;
}
void callback(uint32_t events){
    if(events&ARM_SAI_EVENT_TX_UNDERFLOW)++stream_stats.audio_underruns;
    if(events&ARM_SAI_EVENT_SEND_COMPLETE){uint32_t now=osKernelGetSysTimerCount();
        if(last_irq){uint32_t delta=now-last_irq;if(delta>osKernelGetSysTimerFreq()/1000*11)++stream_stats.deadline_misses;
            if(delta>stream_stats.audio_interval_max)stream_stats.audio_interval_max=delta;
            if(!stream_stats.audio_interval_min||delta<stream_stats.audio_interval_min)stream_stats.audio_interval_min=delta;}
        last_irq=now;++stream_stats.audio_callbacks;
        if(current_ring){if(current_generation)++stream_stats.completed_blocks;consumed.fetch_add(1,std::memory_order_release);osSemaphoreRelease(available);}arm_next();}
}
}
extern "C" int rvtts_audio_init(){
    uint32_t service_error=0;
    if(SERVICES_clocks_enable_clock(se_services_s_handle,CLKEN_HFOSCx2,true,&service_error)!=SERVICES_REQ_SUCCESS||service_error)return -499;
    available=osSemaphoreNew(Slots,Slots,nullptr);if(!available)return -500;
    if(pinconf_set(PORT_(BOARD_DAC_OUTPUT_SDO_GPIO_PORT),BOARD_DAC_OUTPUT_SDO_GPIO_PIN,BOARD_DAC_OUTPUT_SDO_ALTERNATE_FUNCTION,0)||
       pinconf_set(PORT_(BOARD_DAC_OUTPUT_WS_GPIO_PORT),BOARD_DAC_OUTPUT_WS_GPIO_PIN,BOARD_DAC_OUTPUT_WS_ALTERNATE_FUNCTION,0)||
       pinconf_set(PORT_(BOARD_DAC_OUTPUT_SCLK_GPIO_PORT),BOARD_DAC_OUTPUT_SCLK_GPIO_PIN,BOARD_DAC_OUTPUT_SCLK_ALTERNATE_FUNCTION,0)||
       pinconf_set(PORT_(BOARD_WM8904_CODEC_I2C_SDA_GPIO_PORT),BOARD_WM8904_CODEC_I2C_SDA_GPIO_PIN,BOARD_WM8904_CODEC_I2C_SDA_ALTERNATE_FUNCTION,PADCTRL_READ_ENABLE|PADCTRL_DRIVER_DISABLED_PULL_UP)||
       pinconf_set(PORT_(BOARD_WM8904_CODEC_I2C_SCL_GPIO_PORT),BOARD_WM8904_CODEC_I2C_SCL_GPIO_PIN,BOARD_WM8904_CODEC_I2C_SCL_ALTERNATE_FUNCTION,PADCTRL_READ_ENABLE|PADCTRL_DRIVER_DISABLED_PULL_UP))return -501;
    if(WM8904.Initialize()||WM8904.PowerControl(ARM_POWER_FULL))return -502;
    // Board Y4=12.288MHz: SYSCLK/fs=768,16kHz,32-bit I2S slots.
    // WM8904 Rev4.1 p101 Table62 and p94 Table54; schematic220-00319B p7.
    if(WM8904_codec_i2c_write(&i2c_cnfg,0x15,0x1802)||WM8904_codec_i2c_write(&i2c_cnfg,0x19,0x000e))return -503;
    uint16_t rates=0,format=0;
    if(WM8904_codec_i2c_read(&i2c_cnfg,0x15,&rates)||WM8904_codec_i2c_read(&i2c_cnfg,0x19,&format)||rates!=0x1802||format!=0x000e)return -504;
    if(WM8904.SetVolume(60)||WM8904.UnMute())return -505;
    if(Driver_SAI3.Initialize(callback)||Driver_SAI3.PowerControl(ARM_POWER_FULL))return -506;
    if(Driver_SAI3.Control(ARM_SAI_CONFIGURE_TX|ARM_SAI_MODE_MASTER|ARM_SAI_ASYNCHRONOUS|ARM_SAI_PROTOCOL_I2S|ARM_SAI_DATA_SIZE(32),64,16000))return -507;
    if(Driver_SAI3.Control(ARM_SAI_CONTROL_TX,1,0))return -508;return 0;
}
extern "C" int rvtts_audio_write(const int16_t*mono,unsigned generation){
    if(osSemaphoreAcquire(available,1000)!=osOK)return -509;
    uint32_t write=produced.load(std::memory_order_relaxed);auto*dst=ring[write%Slots];
    for(unsigned i=0;i<160;++i){int32_t value=int32_t(mono[i])*65536;dst[2*i]=dst[2*i+1]=value;}
    generations[write%Slots]=generation;produced.store(write+1,std::memory_order_release);
    if(!started&&write+1>=Slots){started=true;arm_next();}return stream_stats.error;
}
