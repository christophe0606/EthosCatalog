// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "embedded_runtime.hpp"
#include "model_pte.h"
#include "model/benchmark_config.h"
#include "kernels/normalization.hpp"
#include "kernels/quantization.hpp"
#include "midi_stage_runtime.hpp"
#include "crc32.hpp"
#include "cmsis_os2.h"
#include "main.h"
#include "se_services_port.h"
#include "RTE_Components.h"
#include CMSIS_device_header
#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include "benchmark_quantization_selftest.hpp"
#ifndef MIDI_BENCHMARK_MODE
#define MIDI_BENCHMARK_MODE 3 // 1: held; 2: full; 3: held then full.
#endif
#ifndef MIDI_FAST_NORMALIZATION
#define MIDI_FAST_NORMALIZATION 1
#endif
#ifndef MIDI_STAGE_DELEGATES
#define MIDI_STAGE_DELEGATES 1
#endif
#ifndef MIDI_SCHEDULE_SWEEP
#define MIDI_SCHEDULE_SWEEP 0 // Set1 to reproduce saved15-run benchmark;0 for DSP development.
#endif
extern "C" int e8_flash_set_read_clock(uint32_t hz);
extern "C" {
volatile uint32_t midi_benchmark_stage, midi_benchmark_complete, model_verified;
volatile uint32_t midi_bank_verified;
volatile int32_t pipeline_error;
uint32_t midi_crc[3],midi_cpu_hz,midi_axi_hz,midi_timer_hz;
uint32_t midi_ospi_hz;
uint64_t midi_startup_ticks,midi_harmonic_load_ticks;
struct MidiTiming {uint64_t ticks;uint32_t invocations,overflow;uint64_t cycles,active;};
MidiTiming midi_method_timing[8][4];
float midi_method_error[8];
float midi_method_rms[8],midi_method_nrmse[8];
float midi_harmonic_component_max[126],midi_harmonic_component_rms[126];
uint32_t midi_accuracy_warning;
uint32_t midi_method_runtime_error;
uint32_t midi_method_weights[8],midi_method_arena[8],midi_method_peak,midi_temp_peak;
struct MidiCacheStats {uint64_t ticks[2];uint32_t calls[2];};
MidiCacheStats midi_cache_stats[8];
void ethosu_cache_profile_record(unsigned operation,uint32_t ticks) {
    if(midi_quantization_current_method<8&&operation<2){auto &s=midi_cache_stats[midi_quantization_current_method];s.ticks[operation]+=ticks;++s.calls[operation];}
}
}
namespace {
alignas(32) uint8_t method_pool[APP_METHOD_POOL_SIZE] __attribute__((section(".bss.ai_pool")));
// Context and z die only after the complete harmonic input has been packed.
// Their storage then becomes the harmonic method's planned tensor arena.
union PhaseStorage {uint8_t harmonic[3072000];struct{float z[1000][320];float context[1000][512];} sequence;};
alignas(32) PhaseStorage phase __attribute__((section(".bss.ai_pool")));
alignas(32) uint8_t temp_pool[APP_TEMP_POOL_SIZE] __attribute__((section(".bss.midi_sram1")));
alignas(32) float input[384000] __attribute__((section(".bss.midi_sram1")));
alignas(32) float controls[1000][126] __attribute__((section(".bss.midi_sram1")));
alignas(32) float packed[1225] __attribute__((section(".bss.midi_sram1")));
arm::embedded::EmbeddedModule *module;
int run_schedule(unsigned instrument,unsigned report_index=~0U);
uint64_t clock64(){
    // Reconstruct the high word from the RTOS millisecond count. This also
    // handles a single method taking longer than the 10.7 s 400 MHz wrap.
    const uint64_t coarse=uint64_t(osKernelGetTickCount())*(midi_timer_hz/osKernelGetTickFreq());
    const uint32_t low=osKernelGetSysTimerCount();
    return coarse+int64_t(int32_t(low-uint32_t(coarse)));
}
uint32_t crc32(const uint8_t *p,size_t n) {return midi_ddsp::crc32(p,n);}
bool finite(float x){uint32_t b;std::memcpy(&b,&x,4);return (b&0x7f800000U)!=0x7f800000U;}
bool prepare_stage(unsigned index){
#if MIDI_STAGE_DELEGATES
    static executorch::runtime::Span<uint8_t> span(phase.harmonic,sizeof(phase.harmonic));
    static executorch::runtime::HierarchicalAllocator planned(executorch::runtime::Span<executorch::runtime::Span<uint8_t>>(&span,1));
    uint8_t *base=index==6?reinterpret_cast<uint8_t*>(phase.sequence.z):reinterpret_cast<uint8_t*>(input);
    const size_t bytes=index==6?sizeof(phase.sequence.z):sizeof(input);
    module=midi_stage_prepare(index,base,bytes,index==7?&planned:nullptr);
#endif
    return module!=nullptr;
}
int run(unsigned index) {
    using namespace executorch::aten;
    const auto &f=midi_fixtures[index];
    const auto *base=reinterpret_cast<const uint8_t*>(MODEL_ASSET_BENCHMARK_FIXTURES_ADDRESS);
    if(!prepare_stage(index))return -601-int(index);
    float *data=index==7?input:packed;
    std::memcpy(data,base+f.input_offset,f.input_count*4);
    std::array<SizesType,3> sizes{f.shape[0],f.shape[1],f.shape[2]};
    std::array<DimOrderType,3> order{0,1,2};
    TensorImpl impl(ScalarType::Float,f.rank,sizes.data(),data,order.data());
    for(unsigned sample=0;sample<4;++sample){
        midi_quantization_current_method=index;
        ethos_benchmark_reset();const uint64_t start=clock64();
        auto result=module->execute(f.name,Tensor(&impl));const uint64_t ticks=clock64()-start;
        if(!result.ok()){midi_method_runtime_error=uint32_t(result.error());return -420-int(index);}
        if(result->size()!=1||!result->front().isTensor())return -430;
        auto out=result->front().toTensor();if(out.scalar_type()!=ScalarType::Float||out.numel()!=f.output_count)return -431;
        const float *actual=out.const_data_ptr<float>(),*expected=reinterpret_cast<const float*>(base+f.output_offset);
        float maximum=0;double squared=0,reference=0;
        for(unsigned i=0;i<f.output_count;++i){if(!finite(actual[i]))return -432;float e=actual[i]-expected[i];maximum=std::max(maximum,std::fabs(e));squared+=double(e)*e;reference+=double(expected[i])*expected[i];
          if(index==7&&sample==0){midi_harmonic_component_max[i%126]=std::max(midi_harmonic_component_max[i%126],std::fabs(e));midi_harmonic_component_rms[i%126]+=e*e;}}
        midi_method_rms[index]=std::sqrt(squared/f.output_count);midi_method_nrmse[index]=std::sqrt(squared/reference);
        midi_method_error[index]=std::max(midi_method_error[index],maximum);
        midi_method_timing[index][sample]={ticks,ethos_benchmark.invocations,ethos_benchmark.overflow,ethos_benchmark.cycles,ethos_benchmark.active_cycles};
        midi_method_weights[index]=ethos_benchmark.weights_size;midi_method_arena[index]=ethos_benchmark.arena_size;
        if(!ethos_benchmark.invocations||ethos_benchmark.overflow||!ethos_benchmark.cycles||ethos_benchmark.cycles!=ethos_benchmark.active_cycles)return -433;
        if(maximum>f.tolerance)midi_accuracy_warning|=1U<<index;
#if !MIDI_STAGE_DELEGATES
        auto usage=arm::embedded::runtime_memory_usage();midi_method_peak=usage.method_peak;midi_temp_peak=usage.temp_peak;
#endif
    }
    return 0;
}
int benchmark(){
    uint32_t service_error=0;
    if(SERVICES_clocks_setting_get(se_services_s_handle,CLOCK_SETTING_EXTSYS0_FREQ,&midi_cpu_hz,&service_error)!=SERVICES_REQ_SUCCESS||service_error)return -410;
    if(SERVICES_clocks_setting_get(se_services_s_handle,CLOCK_SETTING_AXI_FREQ,&midi_axi_hz,&service_error)!=SERVICES_REQ_SUCCESS||service_error)return -411;
    midi_timer_hz=osKernelGetSysTimerFreq();
    if(quantization_selftest())return -620;
    if(e8_flash_set_read_clock(100000000U))return -412;
    midi_ospi_hz=100000000U;
    const uint64_t startup=clock64();
    midi_benchmark_stage=2;
    midi_crc[0]=crc32(model_pte,model_pte_size);
    midi_crc[1]=crc32(reinterpret_cast<const uint8_t*>(MODEL_ASSET_CPU_WEIGHTS_ADDRESS),MODEL_ASSET_CPU_WEIGHTS_SIZE_BYTES);
    midi_crc[2]=crc32(reinterpret_cast<const uint8_t*>(MODEL_ASSET_BENCHMARK_FIXTURES_ADDRESS),MODEL_ASSET_BENCHMARK_FIXTURES_SIZE_BYTES);
    if(midi_crc[0]!=MODEL_PTE_CRC32||midi_crc[1]!=MODEL_ASSET_CPU_WEIGHTS_CRC32||midi_crc[2]!=MODEL_ASSET_BENCHMARK_FIXTURES_CRC32)return -311;
    volatile uint32_t *points[]={reinterpret_cast<uint32_t*>(method_pool),reinterpret_cast<uint32_t*>(method_pool+sizeof(method_pool)-4),reinterpret_cast<uint32_t*>(&phase),reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(&phase)+sizeof(phase)-4),reinterpret_cast<uint32_t*>(temp_pool),reinterpret_cast<uint32_t*>(temp_pool+sizeof(temp_pool)-4),reinterpret_cast<uint32_t*>(input),reinterpret_cast<uint32_t*>(input+384000-1),reinterpret_cast<uint32_t*>(controls),reinterpret_cast<uint32_t*>(&controls[999][125])};
    for(unsigned i=0;i<10;++i)*points[i]=0x14570000U+0x111U*i;
    __DSB();
    for(unsigned i=0;i<10;++i)if(*points[i]!=(0x14570000U+0x111U*i))return -310;
    for(auto point:points)*point=0;
    __DSB();midi_bank_verified=1;
    model_verified=1;midi_benchmark_stage=3;
#if MIDI_STAGE_DELEGATES
    midi_stage_configure(model_pte,model_pte_size,method_pool,sizeof(method_pool),temp_pool,sizeof(temp_pool),clock64);
    midi_startup_ticks=clock64()-startup;
#else
    module=arm::embedded::initialize_runtime(model_pte,model_pte_size,method_pool,sizeof(method_pool),temp_pool,sizeof(temp_pool));
    if(!module)return -400;
    if(MIDI_FAST_NORMALIZATION&&midi_ddsp_install_normalization(module))return -454;
    static executorch::runtime::Span<uint8_t> harmonic_span(phase.harmonic,sizeof(phase.harmonic));
    static executorch::runtime::HierarchicalAllocator harmonic_allocator(executorch::runtime::Span<executorch::runtime::Span<uint8_t>>(&harmonic_span,1));
    const uint64_t load=clock64();
    if(module->load_method("harmonic",&harmonic_allocator)!=executorch::runtime::Error::Ok)return -401;
    midi_harmonic_load_ticks=clock64()-load;midi_startup_ticks=clock64()-startup;
#endif
#if MIDI_BENCHMARK_MODE == 1 || MIDI_BENCHMARK_MODE == 3
    for(unsigned i=0;i<8;++i){midi_benchmark_stage=10+i;int e=run(i);if(e)return e;}
#endif
#if MIDI_BENCHMARK_MODE != 1
    midi_benchmark_stage=20;
#if MIDI_SCHEDULE_SWEEP
    for(unsigned slot=0;slot<15;++slot){int e=run_schedule(slot<3?0:slot-2,slot);if(e)return e;}
#else
    int e=run_schedule(0);if(e)return e;
#endif
#endif
#if MIDI_STAGE_DELEGATES
    midi_stage_release();module=nullptr;
    for(auto &s:midi_stage_stats){midi_method_peak=std::max(midi_method_peak,s.method_peak);midi_temp_peak=std::max(midi_temp_peak,s.temp_peak);}
#else
    auto usage=arm::embedded::runtime_memory_usage();midi_method_peak=usage.method_peak;midi_temp_peak=usage.temp_peak;
#endif
    for(float &r:midi_harmonic_component_rms)r=std::sqrt(r/1000.f);
    midi_benchmark_complete=1;midi_benchmark_stage=30;return 0;
}
}
#include "benchmark_schedule.hpp"
#include "dsp_profile.hpp"
#include "nodes_impl/SendToNetworkRuntime.hpp"
extern "C" int app_main(){midi_benchmark_stage=1;pipeline_error=benchmark();if(!pipeline_error)pipeline_error=profile_dsp();if(!pipeline_error)pipeline_error=dsp_selftest();if(!pipeline_error)pipeline_error=start_demo();return pipeline_error;}
