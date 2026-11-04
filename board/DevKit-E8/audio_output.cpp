// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "audio_output.h"
#include "cmsis_os2.h"
#include "Driver_SAI.h"
#include "WM8904_driver.h"
#include "WM8904_codec_i2c.h"
#include "board_defs.h"
#include "pinconf.h"
#include "se_services_port.h"
#include "RTE_Components.h"
#include "RTE_Device.h"
#include CMSIS_device_header
#include <atomic>
#include <cstring>

extern "C" ARM_DRIVER_SAI Driver_SAI3;
extern "C" ARM_DRIVER_WM8904 WM8904;
extern "C" WM8904_CODEC_SLAVE_I2C_CONFIG i2c_cnfg;
volatile DevKitE8AudioStats devkit_e8_audio_stats{};
namespace {
constexpr unsigned Frames=DEVKIT_E8_AUDIO_FRAMES, Slots=DEVKIT_E8_AUDIO_SLOTS;
constexpr unsigned Items=Frames*2;
// The board RTE uses interrupt-fed I2S3 (DMA disabled). The CPU reads these
// DTCM buffers directly; this implementation intentionally requires that mode.
#if RTE_I2S3_DMA_ENABLE || RTE_I2S3_BLOCKING_MODE_ENABLE
#error "DevKit E8 bounded audio requires interrupt-fed, nonblocking I2S3"
#endif
alignas(32) int32_t ring[Slots][Items], silence[Items];
std::atomic<uint32_t> produced{0}, consumed{0};
std::atomic<bool> running{false};
std::atomic<bool> draining{false};
static_assert(std::atomic<uint32_t>::is_always_lock_free,"ISR queue must be lock free");
osSemaphoreId_t available=nullptr;
bool initialized=false, sai_initialized=false, sai_powered=false, codec_initialized=false;
bool codec_powered=false, current_ring=false, was_starved=false;
uint32_t last_irq=0;
uint32_t timeout_ticks(unsigned ms){
    const uint64_t ticks=(uint64_t(ms)*osKernelGetTickFreq()+999)/1000;
    return uint32_t(ticks>0xfffffffeu?0xfffffffeu:(ticks?ticks:1));
}
int fail(int error){if(!devkit_e8_audio_stats.error)devkit_e8_audio_stats.error=error;return error;}
void arm_next(){
    if(!running.load(std::memory_order_acquire))return;
    const uint32_t read=consumed.load(std::memory_order_relaxed);
    const uint32_t write=produced.load(std::memory_order_acquire);
    current_ring=read!=write;
    const int32_t *data=silence;
    if(current_ring){
        data=ring[read%Slots];was_starved=false;
        if(!devkit_e8_audio_stats.first_audio_ticks)devkit_e8_audio_stats.first_audio_ticks=osKernelGetSysTimerCount();
    }
    else {
        ++devkit_e8_audio_stats.silence_blocks;
        if(!was_starved&&!draining.load(std::memory_order_acquire))++devkit_e8_audio_stats.starvation_events;
        was_starved=true;
    }
    if(Driver_SAI3.Send(data,Items)!=ARM_DRIVER_OK)fail(-910);
}
void callback(uint32_t events){
    if(!running.load(std::memory_order_acquire))return;
    if(events&ARM_SAI_EVENT_TX_UNDERFLOW)++devkit_e8_audio_stats.driver_underflows;
    if(events&ARM_SAI_EVENT_FRAME_ERROR)++devkit_e8_audio_stats.frame_errors;
    if(!(events&ARM_SAI_EVENT_SEND_COMPLETE))return;
    const uint32_t now=osKernelGetSysTimerCount();
    if(last_irq){
        const uint32_t delta=now-last_irq;
        const uint32_t hz=devkit_e8_audio_stats.timer_hz;
        // One block is 16 ms. A 2 ms tolerance records scheduling stalls;
        // the driver does not report every physical FIFO underflow.
        if(uint64_t(delta)*1000>uint64_t(hz)*18)++devkit_e8_audio_stats.deadline_misses;
        if(delta>devkit_e8_audio_stats.interval_max_ticks)devkit_e8_audio_stats.interval_max_ticks=delta;
        if(!devkit_e8_audio_stats.interval_min_ticks||delta<devkit_e8_audio_stats.interval_min_ticks)
            devkit_e8_audio_stats.interval_min_ticks=delta;
    }
    last_irq=now;++devkit_e8_audio_stats.callbacks;
    if(current_ring){
        ++devkit_e8_audio_stats.completed_blocks;
        devkit_e8_audio_stats.last_completed_ticks=now;
        consumed.fetch_add(1,std::memory_order_release);
        if(osSemaphoreRelease(available)!=osOK)fail(-911);
    }
    arm_next();
}
int start(){
    if(running.load(std::memory_order_acquire))return devkit_e8_audio_stats.error;
    devkit_e8_audio_stats.started_ticks=osKernelGetSysTimerCount();
    running.store(true,std::memory_order_release);
    // Enable the TX block before Send, matching the installed driver contract.
    if(Driver_SAI3.Control(ARM_SAI_CONTROL_TX,1,0)!=ARM_DRIVER_OK)return fail(-909);
    arm_next();return devkit_e8_audio_stats.error;
}
}
extern "C" void devkit_e8_audio_stop(){
    running.store(false,std::memory_order_release);
    if(sai_powered){
        Driver_SAI3.Control(ARM_SAI_ABORT_SEND,0,0);
        Driver_SAI3.Control(ARM_SAI_CONTROL_TX,0,0);
        Driver_SAI3.PowerControl(ARM_POWER_OFF);sai_powered=false;
    }
    if(sai_initialized){Driver_SAI3.Uninitialize();sai_initialized=false;}
    if(codec_powered){WM8904.Mute();WM8904.PowerControl(ARM_POWER_OFF);codec_powered=false;}
    if(codec_initialized){WM8904.Uninitialize();codec_initialized=false;}
    if(available){osSemaphoreDelete(available);available=nullptr;}
    initialized=false;
}
extern "C" int devkit_e8_audio_init(){
    if(initialized)return devkit_e8_audio_stats.error;
    devkit_e8_audio_stop();
    std::memset(const_cast<DevKitE8AudioStats*>(&devkit_e8_audio_stats),0,sizeof(devkit_e8_audio_stats));
    devkit_e8_audio_stats.timer_hz=osKernelGetSysTimerFreq();
    produced.store(0,std::memory_order_relaxed);consumed.store(0,std::memory_order_relaxed);
    draining.store(false,std::memory_order_relaxed);
    last_irq=0;current_ring=false;was_starved=false;std::memset(silence,0,sizeof(silence));
    auto failure=[](int error){fail(error);devkit_e8_audio_stop();return error;};
    uint32_t service_error=0;
    if(SERVICES_clocks_enable_clock(se_services_s_handle,CLKEN_HFOSCx2,true,&service_error)!=SERVICES_REQ_SUCCESS||service_error)
        return failure(-900);
    available=osSemaphoreNew(Slots,Slots,nullptr);if(!available)return failure(-901);
    if(pinconf_set(PORT_(BOARD_DAC_OUTPUT_SDO_GPIO_PORT),BOARD_DAC_OUTPUT_SDO_GPIO_PIN,BOARD_DAC_OUTPUT_SDO_ALTERNATE_FUNCTION,0)||
       pinconf_set(PORT_(BOARD_DAC_OUTPUT_WS_GPIO_PORT),BOARD_DAC_OUTPUT_WS_GPIO_PIN,BOARD_DAC_OUTPUT_WS_ALTERNATE_FUNCTION,0)||
       pinconf_set(PORT_(BOARD_DAC_OUTPUT_SCLK_GPIO_PORT),BOARD_DAC_OUTPUT_SCLK_GPIO_PIN,BOARD_DAC_OUTPUT_SCLK_ALTERNATE_FUNCTION,0)||
       pinconf_set(PORT_(BOARD_WM8904_CODEC_I2C_SDA_GPIO_PORT),BOARD_WM8904_CODEC_I2C_SDA_GPIO_PIN,BOARD_WM8904_CODEC_I2C_SDA_ALTERNATE_FUNCTION,PADCTRL_READ_ENABLE|PADCTRL_DRIVER_DISABLED_PULL_UP)||
       pinconf_set(PORT_(BOARD_WM8904_CODEC_I2C_SCL_GPIO_PORT),BOARD_WM8904_CODEC_I2C_SCL_GPIO_PIN,BOARD_WM8904_CODEC_I2C_SCL_ALTERNATE_FUNCTION,PADCTRL_READ_ENABLE|PADCTRL_DRIVER_DISABLED_PULL_UP))return failure(-902);
    if(WM8904.Initialize())return failure(-903);codec_initialized=true;
    if(WM8904.PowerControl(ARM_POWER_FULL))return failure(-903);codec_powered=true;
    // Board Y4=12.288 MHz; SYSCLK/fs=768, 16 kHz, I2S 32-bit slots.
    // WM8904 Rev4.1 pp.94,101 Tables54,62; E8 schematic220-00319-B p.7.
    if(WM8904_codec_i2c_write(&i2c_cnfg,0x15,0x1802)||WM8904_codec_i2c_write(&i2c_cnfg,0x19,0x000e))return failure(-904);
    uint16_t rates=0,format=0;
    if(WM8904_codec_i2c_read(&i2c_cnfg,0x15,&rates)||WM8904_codec_i2c_read(&i2c_cnfg,0x19,&format)||rates!=0x1802||format!=0x000e)return failure(-905);
    // The driver's percentage mapping is floor(percent*63/100). 60 ->37
    // (-20 dB);69 ->43 (-14 dB);78 ->49 (-8 dB). The user requested two
    // successive +6 dB increases, hence +12 dB from the original setting.
    // WM8904 Rev4.1 pp.74-75 Table45, HPOUTL/R_VOL in 1 dB steps.
    if(WM8904.SetVolume(78)||WM8904.UnMute())return failure(-906);
    uint16_t left_volume=0,right_volume=0;
    if(WM8904_codec_i2c_read(&i2c_cnfg,0x39,&left_volume)||
       WM8904_codec_i2c_read(&i2c_cnfg,0x3a,&right_volume)||
       (left_volume&0x3f)!=49||(right_volume&0x3f)!=49)return failure(-915);
    devkit_e8_audio_stats.codec_left_volume_code=left_volume&0x3f;
    devkit_e8_audio_stats.codec_right_volume_code=right_volume&0x3f;
    if(Driver_SAI3.Initialize(callback))return failure(-907);sai_initialized=true;
    if(Driver_SAI3.PowerControl(ARM_POWER_FULL))return failure(-907);sai_powered=true;
    if(Driver_SAI3.Control(ARM_SAI_CONFIGURE_TX|ARM_SAI_MODE_MASTER|ARM_SAI_ASYNCHRONOUS|ARM_SAI_PROTOCOL_I2S|ARM_SAI_DATA_SIZE(32),64,DEVKIT_E8_AUDIO_RATE))return failure(-908);
    initialized=true;return 0;
}
extern "C" int devkit_e8_audio_write(const int16_t *mono,unsigned frames){
    if(!initialized||!mono||frames!=Frames)return -912;
    if(devkit_e8_audio_stats.error)return devkit_e8_audio_stats.error;
    draining.store(false,std::memory_order_release);
    if(osSemaphoreAcquire(available,timeout_ticks(1000))!=osOK){++devkit_e8_audio_stats.write_timeouts;return fail(-913);}
    const uint32_t write=produced.load(std::memory_order_relaxed);
    int32_t *dst=ring[write%Slots];
    for(unsigned i=0;i<Frames;++i){const int32_t value=int32_t(mono[i])*65536;dst[2*i]=dst[2*i+1]=value;}
    const uint32_t occupancy=write+1-consumed.load(std::memory_order_acquire);
    if(occupancy>devkit_e8_audio_stats.queue_high_water)devkit_e8_audio_stats.queue_high_water=occupancy;
    ++devkit_e8_audio_stats.queued_blocks;
    produced.store(write+1,std::memory_order_release);
    if(!running.load(std::memory_order_acquire)&&write+1>=2)return start();
    return devkit_e8_audio_stats.error;
}
extern "C" int devkit_e8_audio_drain(unsigned timeout_ms){
    if(!initialized)return -912;
    draining.store(true,std::memory_order_release);
    if(produced.load(std::memory_order_acquire)!=consumed.load(std::memory_order_acquire)&&!running.load(std::memory_order_acquire)){
        const int error=start();if(error)return error;
    }
    const uint32_t began=osKernelGetTickCount(),limit=timeout_ticks(timeout_ms);
    while(produced.load(std::memory_order_acquire)!=consumed.load(std::memory_order_acquire)){
        if(devkit_e8_audio_stats.error)return devkit_e8_audio_stats.error;
        if(osKernelGetTickCount()-began>=limit)return fail(-914);
        osDelay(1);
    }
    // SEND_COMPLETE can precede the last FIFO sample; two milliseconds exceeds
    // this board's 16-frame FIFO at 16 kHz. Silence maintains the clock meanwhile.
    osDelay(timeout_ticks(2));return devkit_e8_audio_stats.error;
}
