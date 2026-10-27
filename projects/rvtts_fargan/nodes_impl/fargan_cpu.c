// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
// Neural/synthesis bodies are derived from Opus (Amazon, Mozilla, Octasic,
// Jean-Marc Valin), BSD-3-Clause; full notices are preserved in the generated
// fargan_cpu_types.inc and model/upstream/OPUS-COPYING.
#include "fargan_cpu.h"
#include <assert.h>
#include <math.h>
#include <string.h>
#if !defined(RVTTS_FARGAN_HOST)
#include "dsp/statistics_functions.h"
#endif
#if defined(__ARM_FEATURE_MVE) && (__ARM_FEATURE_MVE & 1)
#include <arm_mve.h>
#define CPU_MVE 1
#else
#define CPU_MVE 0
#endif
#define OPUS_INLINE inline
#define OPUS_COPY(d,s,n) memcpy(d,s,(n)*sizeof(*(d)))
#define OPUS_MOVE(d,s,n) memmove(d,s,(n)*sizeof(*(d)))
#define OPUS_CLEAR(d,n) memset(d,0,(n)*sizeof(*(d)))
#define IMAX(a,b) ((a)>(b)?(a):(b))
#define IMIN(a,b) ((a)<(b)?(a):(b))
#define MIN32 IMIN
#define MAX32 IMAX
#define MIN16 IMIN
#define celt_assert assert
#define NB_FEATURES 20
#define NB_BANDS 18
#define PITCH_MAX_PERIOD 256
#define FARGAN_FEATURES 20
#define MAX_RNN_NEURONS_ALL 160
#define MAX_INPUTS 688
#define MAX_CONV_INPUTS_ALL 1024
#define ACTIVATION_LINEAR 0
#define ACTIVATION_SIGMOID 1
#define ACTIVATION_TANH 2
typedef short opus_int16;
typedef struct {
    const int16_t *weights;
    const float *scale, *bias, *float_weights;
    int nb_inputs, nb_outputs;
} LinearLayer;
typedef struct { int unused; } WeightArray;
#include "../model/upstream/activation_lut.h"
#include "fargan_cpu_types.inc"
/* Constants and layout declarations precede the generated model binding. */
#include "fargan_cpu_size.h"
#if defined(RVTTS_FARGAN_HOST)
static _Alignas(32) unsigned char cpu_cache[RVTTS_FARGAN_CPU_SRAM_BYTES];
#else
static _Alignas(32) unsigned char cpu_cache[RVTTS_FARGAN_CPU_SRAM_BYTES] __attribute__((section(".bss.ai_pool")));
#endif
// Ordinary .bss is DTCM on this board; the owning linker keeps SRAM0 sections
// separate. Host builds preserve the same split and offsets in regular memory.
static _Alignas(32) unsigned char cpu_hot_cache[RVTTS_FARGAN_CPU_DTCM_BYTES];
#include "fargan_cpu_data.h"
static FARGAN cpu_model;
volatile uint32_t fargan_cpu_verified, fargan_cpu_crc32, fargan_cpu_kernel_mve=CPU_MVE;
volatile uint32_t fargan_cpu_kernel_rows=4;
volatile uint32_t fargan_cpu_kernel_packed=1;
volatile uint32_t fargan_cpu_dtcm_bytes=RVTTS_FARGAN_CPU_DTCM_BYTES;
_Static_assert(sizeof(FARGANState)<=sizeof(RvttsFarganCpuState), "FARGAN context storage");

static uint32_t cpu_crc32(const unsigned char *p,size_t size) {
    uint32_t crc=0xffffffffu;
    for(size_t i=0;i<size;++i){crc^=p[i];for(unsigned b=0;b<8;++b)crc=(crc>>1)^(0xedb88320u&(0u-(crc&1u)));}
    return ~crc;
}

/* Raw s16 dot products accumulate in s64: 688*32760^2 exceeds s32.
 * MVE reads only complete eight-element groups; the tail is bounded scalar.
 * Dynamic activation scaling is performed once per matrix, with no clipping.
 * Recurrent state, nonlinearities and bias addition remain float32.
 */
static void compute_linear(const LinearLayer *l,float *out,const float *in,int arch) {
    (void)arch;
    _Alignas(16) int16_t quant[MAX_INPUTS];
    float largest=0;
#if !defined(RVTTS_FARGAN_HOST)
    // All prepared matrix input lengths are nonzero multiples of eight. This
    // avoids CMSIS-DSP 1.18.0's full-width tail load for this operation.
    assert(l->nb_inputs>0 && (l->nb_inputs&7)==0);
    arm_absmax_no_idx_f32(in,(uint32_t)l->nb_inputs,&largest);
#else
    for(int i=0;i<l->nb_inputs;++i){float a=fabsf(in[i]);if(a>largest)largest=a;}
#endif
    const float input_scale=largest>1e-30f?largest*(1.0f/32760.0f):1.0f;
    const float inverse=1.0f/input_scale;
    for(int i=0;i<l->nb_inputs;++i)quant[i]=(int16_t)(in[i]*inverse);
    assert(l->nb_inputs>0 && (l->nb_inputs&7)==0);
    int row=0;
    // Offline packing interleaves four rows per eight adjacent columns. This
    // gives the hot loop one sequential weight cursor and one input cursor;
    // all eight core accumulator registers remain available for exact s64 sums.
    for(;row+3<l->nb_outputs;row+=4){
        const int16_t *wp=l->weights+(size_t)row*l->nb_inputs;
        int64_t a0=0,a1=0,a2=0,a3=0;
        for(int i=0;i<l->nb_inputs;i+=8){
#if CPU_MVE
            const int16x8_t x=vld1q_s16(quant+i);
            a0=vmlaldavaq_s16(a0,vld1q_s16(wp),x);wp+=8;
            a1=vmlaldavaq_s16(a1,vld1q_s16(wp),x);wp+=8;
            a2=vmlaldavaq_s16(a2,vld1q_s16(wp),x);wp+=8;
            a3=vmlaldavaq_s16(a3,vld1q_s16(wp),x);wp+=8;
#else
            for(int k=0;k<8;++k){
                a0+=(int64_t)wp[k]*quant[i+k];
                a1+=(int64_t)wp[8+k]*quant[i+k];
                a2+=(int64_t)wp[16+k]*quant[i+k];
                a3+=(int64_t)wp[24+k]*quant[i+k];
            }
            wp+=32;
#endif
        }
        out[row]=(float)a0*(input_scale*l->scale[row])+(l->bias?l->bias[row]:0);
        out[row+1]=(float)a1*(input_scale*l->scale[row+1])+(l->bias?l->bias[row+1]:0);
        out[row+2]=(float)a2*(input_scale*l->scale[row+2])+(l->bias?l->bias[row+2]:0);
        out[row+3]=(float)a3*(input_scale*l->scale[row+3])+(l->bias?l->bias[row+3]:0);
    }
    // Incomplete final row groups are stored row-major, with no padded rows.
    for(;row<l->nb_outputs;++row){
        const int16_t *w=l->weights+(size_t)row*l->nb_inputs;int64_t sum=0;
        for(int i=0;i<l->nb_inputs;++i)sum+=(int64_t)w[i]*quant[i];
        out[row]=(float)sum*(input_scale*l->scale[row])+(l->bias?l->bias[row]:0);
    }
}
static void compute_activation(float *out,const float *in,int n,int activation,int arch) {
    (void)arch;
    if(activation==ACTIVATION_TANH)vec_tanh_lut(out,in,n);
    else if(activation==ACTIVATION_SIGMOID)vec_sigmoid_lut(out,in,n);
    else if(out!=in)memcpy(out,in,(size_t)n*sizeof(float));
}
#include "fargan_cpu_synthesis.inc"

int rvtts_fargan_cpu_bind(const void *weights,size_t size) {
    if(!weights||size!=RVTTS_FARGAN_CPU_BYTES)return -470;
    fargan_cpu_crc32=cpu_crc32((const unsigned char*)weights,size);
    if(fargan_cpu_crc32!=RVTTS_FARGAN_CPU_CRC32)return -471;
    memcpy(cpu_cache,weights,sizeof(cpu_cache));
    memcpy(cpu_hot_cache,(const unsigned char*)weights+sizeof(cpu_cache),sizeof(cpu_hot_cache));
    memset(&cpu_model,0,sizeof(cpu_model));assign_model(&cpu_model);
    fargan_cpu_verified=1;return 0;
}
void rvtts_fargan_cpu_prime(RvttsFarganCpuState *context,const float features[20]) {
    FARGANState *st=(FARGANState*)context;
    memset(context,0,sizeof(*context));st->model=cpu_model;
    float zeros[320]={0},initial[100];
    for(unsigned i=0;i<5;++i)memcpy(initial+i*20,features,20*sizeof(float));
    fargan_cont(st,zeros,initial);
}
void rvtts_fargan_cpu_frame(RvttsFarganCpuState *context,const float features[20],float pcm[160]) {
    fargan_synthesize_impl((FARGANState*)context,pcm,features);
}
void rvtts_fargan_cpu_state(const RvttsFarganCpuState *context,float out[964]) {
    const FARGANState *st=(const FARGANState*)context;
    memcpy(out,st->pitch_buf,256*4);memcpy(out+256,st->cond_conv1_state,128*4);memcpy(out+384,st->fwc0_mem,164*4);
    memcpy(out+548,st->gru1_state,160*4);memcpy(out+708,st->gru2_state,128*4);memcpy(out+836,st->gru3_state,128*4);
}
