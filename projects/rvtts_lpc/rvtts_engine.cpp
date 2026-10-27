// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
// Feature construction and scan derived from rvTTS by Armstrong Subero,
// Apache-2.0; original notices are preserved in model/upstream/.
#include "rvtts_engine.hpp"
#include "embedded_runtime.hpp"
#include "model_pte.h"
#include "model/upstream/prosody_weights_int8_meta.h"
#include "model/upstream/acoustic_stage2_weights_int8_meta.h"
#include "cmsis_os2.h"
#include "main.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
extern "C" { volatile uint32_t model_verified,model_crc32; }
namespace rvtts {
const char *const names[MethodCount]={"prosody_pre","prosody_gru","prosody_head","acoustic"};
const unsigned input_sizes[MethodCount]={5888,256,4096,17000};
const unsigned output_sizes[MethodCount]={2048,128,192,544};
namespace {
// Read the PTE from OSPI once. Both command stream and neural weights then use
// the NPU SRAM ports; the stored model and its quantization remain unchanged.
alignas(32) uint8_t pte_cache[MODEL_PTE_SIZE_BYTES] __attribute__((section(APP_POOL_SECTION)));
alignas(32) uint8_t method_pool[APP_METHOD_POOL_SIZE] __attribute__((section(APP_POOL_SECTION)));
alignas(32) uint8_t temp_pool[APP_TEMP_POOL_SIZE] __attribute__((section(APP_POOL_SECTION)));
alignas(32) float input[17000] __attribute__((section(APP_POOL_SECTION)));
alignas(32) float pre[MaxFrames][64] __attribute__((section(APP_POOL_SECTION)));
alignas(32) float hidden[MaxFrames][128] __attribute__((section(APP_POOL_SECTION)));
alignas(32) float cond[MaxFrames][3] __attribute__((section(APP_POOL_SECTION)));
alignas(32) float acoustic_input[MaxFrames][170] __attribute__((section(APP_POOL_SECTION)));
alignas(32) float result[2048] __attribute__((section(APP_POOL_SECTION)));
arm::embedded::EmbeddedModule *module;
uint32_t crc32(const uint8_t *p,size_t size) {
    uint32_t crc=0xffffffffU;
    for(size_t i=0;i<size;++i) {crc^=p[i];for(unsigned b=0;b<8;++b) crc=(crc>>1)^(0xedb88320U&(0U-(crc&1U)));}
    return ~crc;
}
bool finite(float value) { uint32_t bits;std::memcpy(&bits,&value,4);return (bits&0x7f800000U)!=0x7f800000U; }
constexpr uint64_t vowels=(1ULL<<2)|(1ULL<<3)|(1ULL<<4)|(1ULL<<5)|(1ULL<<6)|(1ULL<<7)|(1ULL<<12)|(1ULL<<13)|(1ULL<<14)|(1ULL<<18)|(1ULL<<19)|(1ULL<<26)|(1ULL<<27)|(1ULL<<34)|(1ULL<<35);
constexpr uint64_t fricatives=(1ULL<<15)|(1ULL<<36)|(1ULL<<30)|(1ULL<<39)|(1ULL<<31)|(1ULL<<40)|(1ULL<<33)|(1ULL<<11)|(1ULL<<17);
constexpr uint64_t stops=(1ULL<<8)|(1ULL<<10)|(1ULL<<16)|(1ULL<<28)|(1ULL<<32)|(1ULL<<21);
constexpr uint64_t affricates=(1ULL<<9)|(1ULL<<20),nasals=(1ULL<<23)|(1ULL<<24)|(1ULL<<25);
constexpr uint64_t liquids=(1ULL<<22)|(1ULL<<29),glides=(1ULL<<37)|(1ULL<<38);
constexpr uint64_t sonorants=vowels|nasals|liquids|glides;
constexpr uint64_t voiced=sonorants|(1ULL<<8)|(1ULL<<10)|(1ULL<<16)|(1ULL<<36)|(1ULL<<39)|(1ULL<<40)|(1ULL<<11)|(1ULL<<20);
void embedding(const uint8_t *blob,unsigned offset,unsigned id,unsigned count,float scale,float *dst) {
    const int8_t *src=reinterpret_cast<const int8_t*>(blob+offset)+id*count;
    for(unsigned i=0;i<count;++i)dst[i]=src[i]*scale;
}
void build(const uint8_t *phones,const uint8_t *stress,const uint16_t *words,
           unsigned t,unsigned n,bool prosody,float *dst) {
    unsigned start=t,end=t+1;while(start&&phones[start-1]==phones[t])--start;
    while(end<n&&phones[end]==phones[t])++end;
    unsigned cp=phones[t]<41?phones[t]:0,pp=start?phones[start-1]:0,np=end<n?phones[end]:0;
    if(pp>=41)pp=0;if(np>=41)np=0;
    const uint8_t *blob=reinterpret_cast<const uint8_t*>(prosody?MODEL_ASSET_PROSODY_SOURCE_ADDRESS:MODEL_ASSET_ACOUSTIC_SOURCE_ADDRESS);
    float ps=prosody?PW8_SCALE_PHONE_EMB:AS2Q_SCALE_PHONE_EMB,ss=prosody?PW8_SCALE_STRESS_EMB:AS2Q_SCALE_STRESS_EMB;
    unsigned p=0;
    const unsigned ids[]={pp,cp,np};for(unsigned id:ids){embedding(blob,16,id,48,ps,dst+p);p+=48;}
    unsigned sid=stress&&stress[t]<4?stress[t]:0;embedding(blob,1984,sid,8,ss,dst+p);p+=8;
    if(prosody){unsigned wid=words&&words[t]<14682?words[t]:0;embedding(blob,PW8_OFF_WORD_EMB,wid,8,PW8_SCALE_WORD_EMB,dst+p);p+=8;}
    const uint64_t masks[]={vowels,fricatives,stops,affricates,nasals,liquids,glides,sonorants,voiced,3};
    for(uint64_t mask:masks)dst[p++]=((mask>>cp)&1U)?1.0f:0.0f;
    unsigned length=end-start;float pos=length>1?float(t-start)/float(length-1):0;
    dst[p++]=pos;dst[p++]=1-pos;dst[p++]=std::fmin(float(length)/30,2.0f);dst[p++]=std::log(1.0f+length)/5;
    if(prosody){float u=float(t)/float(n>1?n-1:1);dst[p++]=u;dst[p++]=1-u;dst[p++]=u>.85f?(u-.85f)/.15f:0;
        dst[p++]=0;dst[p++]=n*.01f/30;dst[p++]=cp>1?1:0;dst[p++]=.2f;dst[p++]=u;dst[p++]=pos;dst[p++]=0;
    }else{const float *mean=reinterpret_cast<const float*>(blob+AS2Q_OFF_NORM_MEAN),*sd=reinterpret_cast<const float*>(blob+AS2Q_OFF_NORM_STD);
        for(unsigned j=0;j<3;++j){unsigned k=j?j+17:0;dst[p++]=(cond[t][j]-mean[k])/sd[k];}dst[p]=1;}
}
}
int initialize() {
    model_crc32=crc32(model_pte,model_pte_size);
    if(model_pte_size!=MODEL_PTE_SIZE_BYTES||model_crc32!=MODEL_PTE_CRC32)return -311;
    if(crc32(reinterpret_cast<const uint8_t*>(MODEL_ASSET_PROSODY_SOURCE_ADDRESS),MODEL_ASSET_PROSODY_SOURCE_SIZE_BYTES)!=MODEL_ASSET_PROSODY_SOURCE_CRC32)return -312;
    if(crc32(reinterpret_cast<const uint8_t*>(MODEL_ASSET_ACOUSTIC_SOURCE_ADDRESS),MODEL_ASSET_ACOUSTIC_SOURCE_SIZE_BYTES)!=MODEL_ASSET_ACOUSTIC_SOURCE_CRC32)return -313;
    if(crc32(reinterpret_cast<const uint8_t*>(MODEL_ASSET_DICTIONARY_ADDRESS),MODEL_ASSET_DICTIONARY_SIZE_BYTES)!=MODEL_ASSET_DICTIONARY_CRC32)return -314;
    if(crc32(reinterpret_cast<const uint8_t*>(MODEL_ASSET_WORDS_ADDRESS),MODEL_ASSET_WORDS_SIZE_BYTES)!=MODEL_ASSET_WORDS_CRC32)return -315;
    std::memcpy(pte_cache,model_pte,model_pte_size);
    if(crc32(pte_cache,model_pte_size)!=MODEL_PTE_CRC32)return -316;
    model_verified=1;module=arm::embedded::initialize_runtime(pte_cache,model_pte_size,method_pool,sizeof(method_pool),temp_pool,sizeof(temp_pool));
    if(!module)return -400;
    for(unsigned i=0;i<MethodCount;++i)if(module->load_method(names[i])!=executorch::runtime::Error::Ok)return -401-static_cast<int>(i);
    return 0;
}
int execute(unsigned method,const float *src,float *dst,Timing *timing) {
    using namespace executorch::aten;
    if(method>=MethodCount||!module||!src||!dst)return -420;
    if(src!=input)std::memcpy(input,src,input_sizes[method]*4);
    std::array<SizesType,3> sizes{32,184,0};unsigned rank=2;
    if(method==Gru){sizes={1,256,0};}else if(method==Head){sizes={32,128,0};}else if(method==Acoustic){sizes={1,100,170};rank=3;}
    std::array<DimOrderType,3> order{0,1,2};TensorImpl impl(ScalarType::Float,rank,sizes.data(),input,order.data());
    ethos_benchmark_reset();uint32_t start=osKernelGetSysTimerCount();
    auto out=module->execute(names[method],Tensor(&impl));uint32_t ticks=osKernelGetSysTimerCount()-start;
    if(!out.ok()||out->size()!=1||!out->front().isTensor())return -421;
    auto tensor=out->front().toTensor();if(tensor.scalar_type()!=ScalarType::Float||tensor.numel()!=output_sizes[method])return -422;
    std::memcpy(dst,tensor.const_data_ptr<float>(),output_sizes[method]*4);
    for(unsigned i=0;i<output_sizes[method];++i)if(!finite(dst[i]))return -423;
    if(ethos_benchmark.invocations!=1||ethos_benchmark.overflow||!ethos_benchmark.cycles||ethos_benchmark.cycles!=ethos_benchmark.active_cycles)return -424;
    if(timing)*timing={ticks,ethos_benchmark.cycles,ethos_benchmark.invocations,ethos_benchmark.overflow};
    return 0;
}
int feature_stage(unsigned stage,const uint8_t *phones,const uint8_t *stress,const uint16_t *words,unsigned n,float *out,Timing *timing) {
    if(!phones||!n||n>MaxFrames||!out)return -430;
    Timing sum{};uint32_t start=osKernelGetSysTimerCount();
    auto call=[&](unsigned method)->int{Timing t{};int e=execute(method,input,result,&t);sum.cycles+=t.cycles;sum.invocations+=t.invocations;sum.overflow+=t.overflow;return e;};
    if(stage==Pre) for(unsigned s=0;s<n;s+=32){std::memset(input,0,5888*4);unsigned count=std::min(32U,n-s);for(unsigned j=0;j<count;++j)build(phones,stress,words,s+j,n,true,input+j*184);int e=call(Pre);if(e)return e;std::memcpy(pre[s],result,count*64*4);}
    float state[128]={};
    if(stage==Gru) for(unsigned t=0;t<n;++t){std::memcpy(input,pre[t],256);std::memcpy(input+64,pre[n-1-t],256);std::memcpy(input+128,state,512);int e=call(Gru);if(e)return e;std::memcpy(state,result,512);std::memcpy(hidden[t],state,256);std::memcpy(hidden[n-1-t]+64,state+64,256);}
    if(stage==Head) for(unsigned s=0;s<n;s+=32){std::memset(input,0,4096*4);unsigned count=std::min(32U,n-s);std::memcpy(input,hidden[s],count*128*4);int e=call(Head);if(e)return e;for(unsigned j=0;j<count;++j)std::memcpy(cond[s+j],result+j*6,12);}
    if(stage==Acoustic) for(unsigned t=0;t<n;++t){build(phones,stress,words,t,n,false,acoustic_input[t]);out[t*20]=cond[t][0];out[t*20+18]=cond[t][1];out[t*20+19]=cond[t][2];}
    if(stage==Acoustic) for(unsigned s=0;s<n;s+=32){std::memset(input,0,sizeof(input));for(unsigned j=0;j<100;++j){int frame=int(s)+int(j)-34;if(frame>=0&&unsigned(frame)<n)std::memcpy(input+j*170,acoustic_input[frame],680);}int e=call(Acoustic);if(e)return e;unsigned count=std::min(32U,n-s);for(unsigned j=0;j<count;++j)std::memcpy(out+(s+j)*20+1,result+j*17,68);}
    sum.ticks=osKernelGetSysTimerCount()-start;if(timing)*timing=sum;return 0;
}
int features(const uint8_t *p,const uint8_t *s,const uint16_t *w,unsigned n,float *out,Timing *timing) {
    Timing sum{}; const uint32_t start=osKernelGetSysTimerCount();
    for(unsigned stage=0;stage<MethodCount;++stage) {Timing t{};int e=feature_stage(stage,p,s,w,n,out,&t);if(e)return e;sum.cycles+=t.cycles;sum.invocations+=t.invocations;sum.overflow+=t.overflow;}
    sum.ticks=osKernelGetSysTimerCount()-start;if(timing)*timing=sum;return 0;
}
}
