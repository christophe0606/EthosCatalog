// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "midi_stage_runtime.hpp"
#include "kernels/normalization.hpp"
#include "model/benchmark_config.h"
#include "model/staging_manifest.h"
#include "crc32.hpp"
#include <executorch/runtime/platform/runtime.h>
#include "RTE_Components.h"
#include CMSIS_device_header
#include <algorithm>
#include <cstring>
#include <memory>
using namespace executorch::runtime;
extern "C" {
MidiStageStats midi_stage_stats[8];
uint32_t midi_stage_routes[4];
int32_t midi_stage_error;
uint32_t midi_stage_last_segment[4];
// The pinned driver explicitly supports this weak project override. Select
// profile1 (SRAM,0x20) only for staged command/constant addresses. Keep known
// OSPI profiles2/3 (0x24) and the existing writable-arena profile0.
unsigned int ethosu_config_select(uint64_t address,int index) {
    const bool bank0=address>=0x02000000ULL&&address<0x02400000ULL;
    const bool bank1=address>=0x02400000ULL&&address<0x02800000ULL;
    unsigned selection=0;
    if(index==-1||index==0)selection=(bank0||bank1)?1U:(index==-1?2U:3U);
    ++midi_stage_routes[selection];return selection;
}
}
namespace {
class PeakAllocator final:public MemoryAllocator {
public:PeakAllocator(size_t n,uint8_t *p):MemoryAllocator(n,p){}
void *allocate(size_t n,size_t alignment=kDefaultAlignment)override{auto *p=MemoryAllocator::allocate(n,alignment);peak=std::max(peak,used_size());return p;}
size_t peak=0;
};
uint32_t crc32(const uint8_t *data,size_t n) {
    return midi_ddsp::crc32(data,n);
}
struct Configuration {const uint8_t *pte;size_t bytes;uint8_t *method,*temp;size_t method_bytes,temp_bytes;uint64_t (*clock)();} config;
class StagedLoader final:public DataLoader {
public:
    StagedLoader(unsigned stage,uint8_t *base,size_t bytes,PeakAllocator *constants):stage_(stage),base_(base),bytes_(bytes),constants_(constants){}
    Result<FreeableBuffer> load(size_t offset,size_t size,const SegmentInfo &info)const override {
        midi_stage_last_segment[0]=stage_;midi_stage_last_segment[1]=offset;midi_stage_last_segment[2]=size;midi_stage_last_segment[3]=unsigned(info.segment_type);
        if(offset>config.bytes||size>config.bytes-offset)return Error::InvalidArgument;
        const bool backend=info.segment_type==SegmentInfo::Type::Backend&&stage_<7;
        const bool constant=info.segment_type==SegmentInfo::Type::Constant;
        if(!backend&&!constant)return FreeableBuffer(config.pte+offset,size,nullptr);
        for(unsigned i=0;i<entries_;++i)if(cache_[i].offset==offset&&cache_[i].size==size)return FreeableBuffer(cache_[i].data,size,nullptr);
        if(entries_==32)return Error::MemoryAllocationFailed;
        uint8_t *dest=nullptr;
        if(backend){const size_t aligned=(used_+31)&~size_t(31);if(aligned>bytes_||size>bytes_-aligned)return Error::MemoryAllocationFailed;dest=base_+aligned;used_=aligned+size;}
        else {
            // Bound constant caching so metadata and the loaded method retain
            // headroom. Large/unrecognized constant requests remain in OSPI.
            if(size>131072||constants_->used_size()+size+131072>config.method_bytes){++midi_stage_stats[stage_].constant_bypasses;return FreeableBuffer(config.pte+offset,size,nullptr);}
            dest=static_cast<uint8_t*>(constants_->allocate(size,32));if(!dest)return Error::MemoryAllocationFailed;
        }
        auto &stats=midi_stage_stats[stage_];
        const uint64_t start=config.clock();std::memcpy(dest,config.pte+offset,size);
        const uint64_t copied=config.clock();stats.memcpy_ticks+=copied-start;
        // CPU writes can reside in write-back cache. Publish staged command
        // and constant data before any NPU method can retain those pointers.
        // The board invoke hook also cleans cache; this boundary is explicit.
        SCB_CleanDCache();__DSB();
        const uint64_t cleaned=config.clock();stats.cache_clean_ticks+=cleaned-copied;
        // Expected segment CRCs come from the complete already-verified PTE.
        const MidiSegmentManifest *expected=nullptr;
        for(const auto &s:midi_segment_manifest)if(s.offset==offset&&s.bytes==size){expected=&s;break;}
        if(!expected||crc32(dest,size)!=expected->crc){++midi_stage_stats[stage_].crc_failures;return Error::InvalidProgram;}
        const uint64_t checked=config.clock();stats.crc_ticks+=checked-cleaned;stats.copy_ticks+=checked-start;
        if(backend)stats.backend_bytes+=size;else stats.constant_bytes+=size;
        cache_[entries_++]={offset,size,dest};return FreeableBuffer(dest,size,nullptr);
    }
    Result<size_t> size()const override{return config.bytes;}
    Error load_into(size_t offset,size_t size,const SegmentInfo &info,void *buffer)const override {
        if(!buffer)return Error::InvalidArgument;auto r=load(offset,size,info);if(!r.ok())return r.error();std::memcpy(buffer,r->data(),size);return Error::Ok;
    }
private:
    struct Entry{size_t offset,size;uint8_t *data;};
    unsigned stage_;uint8_t *base_;size_t bytes_;PeakAllocator *constants_;
    mutable Entry cache_[32]{};mutable unsigned entries_=0;mutable size_t used_=0;
};
std::unique_ptr<arm::embedded::EmbeddedModule> active;
PeakAllocator *method_usage,*temp_usage;
unsigned current=8;
}
void midi_stage_configure(const unsigned char *pte,size_t bytes,uint8_t *method,size_t method_bytes,uint8_t *temp,size_t temp_bytes,uint64_t (*clock)()) {
    config={pte,bytes,method,temp,method_bytes,temp_bytes,clock};
    static bool initialized=false;if(!initialized){runtime_init();initialized=true;}
}
void midi_stage_release() {
    if(active){auto &s=midi_stage_stats[current];s.method_peak=std::max(s.method_peak,uint32_t(method_usage->peak));s.temp_peak=std::max(s.temp_peak,uint32_t(temp_usage->peak));active.reset();}
    method_usage=nullptr;temp_usage=nullptr;current=8;
}
arm::embedded::EmbeddedModule *midi_stage_prepare(unsigned index,uint8_t *staging,size_t staging_bytes,HierarchicalAllocator *planned) {
    if(index>7){midi_stage_error=-600;return nullptr;}
    if(active&&current==index)return active.get();
    const uint64_t start=config.clock();midi_stage_release();
    auto method=std::make_unique<PeakAllocator>(config.method_bytes,config.method);auto temp=std::make_unique<PeakAllocator>(config.temp_bytes,config.temp);
    method_usage=method.get();temp_usage=temp.get();
    auto loader=std::make_unique<StagedLoader>(index,staging,staging_bytes,method_usage);
    active=std::make_unique<arm::embedded::EmbeddedModule>(config.pte,config.bytes,std::move(loader),std::move(method),std::move(temp));current=index;
    if(midi_ddsp_install_normalization(active.get())){midi_stage_error=-454;return nullptr;}
    auto error=active->load_method(midi_fixtures[index].name,planned);
    if(error!=Error::Ok){midi_stage_error=int32_t(error);return nullptr;}
    midi_stage_stats[index].prepare_ticks+=config.clock()-start;++midi_stage_stats[index].loads;return active.get();
}
