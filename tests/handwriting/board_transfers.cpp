// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
// Opt-in E8 benchmark; compiled out of normal application builds.
#if defined(HANDWRITING_TRANSFER_BENCHMARK)
#include "nodes_impl/handwriting_engine.hpp"
#include "Driver_DMA.h"
#include "RTE_Components.h"
#include CMSIS_device_header
#include "sys_utils.h"
#include "cache.h"
#include "cmsis_os2.h"
#include <cstdio>
#include <cstring>
#include <initializer_list>

extern "C" ARM_DRIVER_DMA Driver_DMA1;
namespace handwriting { bool benchmark_copy_output=false; }
namespace {
constexpr unsigned Samples=64, Capacity=6464;
alignas(32) unsigned char tcm_source[Capacity], tcm_destination[Capacity];
alignas(32) unsigned char sram_source[Capacity] __attribute__((section(".bss.ai_pool")));
volatile uint32_t completion;
volatile uint32_t consumer;
void completed(uint32_t event,int8_t) { completion=event; }
__attribute__((noinline)) void cpu_copy(void *dst,const void *src,unsigned bytes) {
    std::memcpy(dst,src,bytes);
    __DSB();
}
int dma_copy(DMA_Handle_Type &handle,void *dst,const void *src,unsigned bytes) {
    ARM_DMA_PARAMS params{};
    params.peri_reqno=-1; params.burst_len=16; params.burst_size=BS_BYTE_4;
    params.dir=ARM_DMA_MEM_TO_MEM; params.src_addr=src; params.dst_addr=dst;
    params.num_bytes=bytes; params.irq_priority=5; params.cb_event=completed;
    // Preserve dirty bytes in partial destination cache lines. The driver
    // cleans the source and invalidates the destination before/after DMA.
    RTSS_CleanDCache_by_Addr(dst,bytes);
    completion=0;
    const uint32_t start=osKernelGetSysTimerCount();
    int error=Driver_DMA1.Start(&handle,&params);
    if(error) return error;
    while(!completion) {
        if(uint32_t(osKernelGetSysTimerCount()-start)>osKernelGetSysTimerFreq()/10) {
            Driver_DMA1.Stop(&handle); return -440;
        }
    }
    __DSB();
    return completion==ARM_DMA_EVENT_COMPLETE?0:-441;
}
int transfers(DMA_Handle_Type &handle,const char *name,const unsigned char *src,
              unsigned offset,unsigned bytes) {
    uint64_t cpu=0,dma=0;
    auto *dst=tcm_destination+offset;
    src+=offset;
    for(unsigned sample=0;sample<Samples+1;++sample) {
        // Reset the destination each trial; verify the whole buffer, including
        // the bytes immediately before/after unaligned packed-input slices.
        for(unsigned backend=0;backend<2;++backend) {
            std::memset(tcm_destination,0xa5,Capacity);
            const uint32_t start=osKernelGetSysTimerCount();
            int error=0;
            if(backend) error=dma_copy(handle,dst,src,bytes);
            else cpu_copy(dst,src,bytes);
            const uint32_t elapsed=osKernelGetSysTimerCount()-start;
            if(error) return error;
            if(std::memcmp(dst,src,bytes)) return -442;
            for(unsigned i=0;i<Capacity;++i)
                if((i<offset || i>=offset+bytes) && tcm_destination[i]!=0xa5) return -443;
            consumer+=dst[bytes-1];
            if(sample) (backend?dma:cpu)+=elapsed;
        }
    }
    printf("TRANSFER %s bytes=%u offset=%u samples=%u cpu_ticks=%llu dma_ticks=%llu\n",
        name,bytes,offset,Samples,(unsigned long long)cpu,(unsigned long long)dma);
    return 0;
}
int steps(handwriting::Engine &engine,unsigned trial,bool copy) {
    handwriting::benchmark_copy_output=copy;
    constexpr char sentence[]="The quick brown fox jumps over the lazy dog Pack a box with five dozen jugs";
    static_assert(sizeof(sentence)-1==handwriting::MaxCharacters);
    int error=engine.begin(sentence,42);
    handwriting::Stroke point{};
    if(!error) error=engine.step(point);
    uint64_t ticks=0;
    uint32_t hash=2166136261U;
    for(unsigned i=0;i<Samples && !error;++i) {
        const uint32_t start=osKernelGetSysTimerCount();
        error=engine.step(point);
        ticks+=uint32_t(osKernelGetSysTimerCount()-start);
        const auto *raw=reinterpret_cast<const unsigned char*>(&point);
        for(unsigned j=0;j<sizeof(point);++j) hash=(hash^raw[j])*16777619U;
    }
    if(!error && hash!=0x65bfb6d3U) error=-444;
    printf("TRANSFER_STEP mode=%s trial=%u samples=%u ticks=%llu stroke_hash=%08lx error=%d engine_bytes=%u\n",
        copy?"copy":"view",trial,Samples,(unsigned long long)ticks,(unsigned long)hash,error,unsigned(sizeof(engine)));
    handwriting::benchmark_copy_output=false;
    return error;
}
}
int handwriting_transfer_benchmark(handwriting::Engine &engine) {
    printf("TRANSFER timer_hz=%lu\n",(unsigned long)osKernelGetSysTimerFreq());
    // Exact application packing offsets and dense-output lengths. Recurrent
    // state and packed inputs live in DTCM; dequantized outputs live in SRAM0.
    for(unsigned i=0;i<Capacity;++i) tcm_source[i]=sram_source[i]=uint8_t(i*37+11);
    int error=Driver_DMA1.Initialize(); if(error) return error;
    error=Driver_DMA1.PowerControl(ARM_POWER_FULL); if(error) return error;
    DMA_Handle_Type handle=-1;
    error=Driver_DMA1.Allocate(&handle); if(error) return error;
    const unsigned sizes[]={12,292,1600,1600,1600,1600};
    const unsigned offsets[]={292,0,304,12,1612,1904};
    for(unsigned i=0;i<6 && !error;++i)
        error=transfers(handle,"DTCM-DTCM",tcm_source,offsets[i],sizes[i]);
    for(unsigned bytes:{120U,484U,6400U})
        if(!error) error=transfers(handle,"SRAM-DTCM",sram_source,0,bytes);
    Driver_DMA1.DeAllocate(&handle);
    Driver_DMA1.PowerControl(ARM_POWER_OFF);
    Driver_DMA1.Uninitialize();
    if(error) return error;
    // Same-binary comparison isolates output copying. Both modes use the new
    // input packing. Alternate order and compare the original trajectory hash.
    for(unsigned trial=0;trial<3 && !error;++trial) {
        error=steps(engine,trial,trial%2==0);
        if(!error) error=steps(engine,trial,trial%2!=0);
    }
    return error;
}
#endif
