// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "camera_test_support.h"
#include <assert.h>
#include <stdio.h>
extern int camera_init(void),camera_capture(uint16_t*,uint32_t);
extern volatile uint32_t camera_frames,camera_errors,camera_timeouts;
static void (*notify)(uint32_t);
static unsigned configured,enabled,dma,stops,cleaned,invalidated,flags,failure;
static int initialize(void (*cb)(uint32_t)) { notify=cb; return 0; }
static int power(int p) {
    assert(p==ARM_POWER_FULL);
    notify(ARM_CPI_EVENT_MIPI_CSI2_ERROR); return 0;
}
static int control(uint32_t command,uint32_t value) {
    (void)value; assert(!dma);
    if(command==CPI_CONFIGURE) configured=1;
    if(command==CPI_EVENTS_CONFIGURE) enabled=1;
    return 0;
}
static int capture(uint8_t *p) {
    assert(p && configured && enabled && !dma && cleaned>camera_frames);
    if(failure==3) return -91;
    dma=1; return 0;
}
static int stop(void) {
    assert(dma); dma=0; configured=enabled=0; ++stops; return failure==4?-92:0;
}
ARM_DRIVER_CPI Driver_CPI={initialize,power,control,capture,stop};
osEventFlagsId_t osEventFlagsNew(const void *p) { (void)p; return (void*)1; }
uint32_t osEventFlagsSet(osEventFlagsId_t p,uint32_t n) { assert(p); return flags|=n; }
uint32_t osEventFlagsClear(osEventFlagsId_t p,uint32_t n) { assert(p); return flags&=~n; }
uint32_t osEventFlagsWait(osEventFlagsId_t p,uint32_t n,uint32_t mode,uint32_t ticks) {
    assert(p && n==3 && mode==osFlagsWaitAny && ticks==2000 && dma);
    if(failure==1) return osFlagsError;
    notify(failure==2?ARM_CPI_EVENT_ERR_HARDWARE:ARM_CPI_EVENT_CAMERA_CAPTURE_STOPPED);
    return flags;
}
void SCB_CleanInvalidateDCache_by_Addr(uint32_t *p,uint32_t n) {
    assert(p && n==BOARD_CAMERA_BYTES && !dma); ++cleaned;
}
void SCB_InvalidateDCache_by_Addr(uint32_t *p,uint32_t n) {
    assert(p && n==BOARD_CAMERA_BYTES && !dma); ++invalidated;
}
void __DSB(void) {}
int main(void) {
    uint16_t frame[32]; assert(camera_init()==0);
    for(unsigned i=0;i<100;i++) assert(camera_capture(frame,2000)==0);
    assert(camera_frames==100 && stops==100 && invalidated==100 && !dma);
    failure=1; assert(camera_capture(frame,2000)==-812 && camera_timeouts==1 && !dma);
    failure=2; assert(camera_capture(frame,2000)==-813 && camera_errors==2 && !dma);
    failure=3; assert(camera_capture(frame,2000)==-91 && !dma);
    failure=4; assert(camera_capture(frame,2000)==-92 && !dma);
    failure=0; assert(camera_capture(frame,2000)==0 && camera_frames==101);
    assert(invalidated==101);
    assert(camera_diagnostics.error_count[CAMERA_INITIALIZING]==1);
    assert(camera_diagnostics.error_mask[CAMERA_INITIALIZING]==ARM_CPI_EVENT_MIPI_CSI2_ERROR);
    assert(camera_diagnostics.error_count[CAMERA_CAPTURING]==1);
    assert(camera_diagnostics.phase==CAMERA_IDLE);
    puts("PASS: production camera adapter rearms after Stop, exclusive DMA/cache ownership and errors");
    return 0;
}
