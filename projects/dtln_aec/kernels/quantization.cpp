// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "quantization.hpp"
#include "quantization_loops.hpp"
#include <executorch/runtime/kernel/kernel_runtime_context.h>
#include "cmsis_os2.h"
#include <optional>
#include <cfenv>
#include <cstring>
#ifndef DTLN_FAST_DEQUANTIZE
// The pinned portable path is faster on the measured M55 image; retain it.
#define DTLN_FAST_DEQUANTIZE 0
#endif
using namespace executorch::runtime;
using namespace executorch::aten;
namespace torch::executor::native {
Tensor &quantize_per_tensor_out(KernelRuntimeContext &,const Tensor &,double,
    int64_t,int64_t,int64_t,ScalarType,Tensor &);
Tensor &dequantize_per_tensor_out(KernelRuntimeContext &,const Tensor &,double,
    int64_t,int64_t,int64_t,ScalarType,std::optional<ScalarType>,Tensor &);
}
DtlnQuantizationStats dtln_quantization_stats[9]{};
uint32_t dtln_quantization_current_method=8;
namespace {
bool finite_positive(float value) {
    uint32_t bits;
    std::memcpy(&bits,&value,sizeof(bits));
    return bits != 0 && bits < 0x7f800000U;
}
bool dense(const Tensor &a) {
    if (a.numel() <= 0) return false;
    int64_t stride=1;
    for (int d=int(a.dim())-1;d>=0;--d) {
        if (a.size(d)>1 && a.strides()[d]!=stride) return false;
        stride*=a.size(d);
    }
    return true;
}
bool compatible(const Tensor &a,const Tensor &b) {
    if (!dense(a)||!dense(b)||a.dim()!=b.dim()) return false;
    for (int d=0;d<a.dim();++d) if (a.size(d)!=b.size(d)) return false;
    // The fast loop uses static destinations and does not resize or alias.
    const auto ap=reinterpret_cast<uintptr_t>(a.const_data_ptr());
    const auto bp=reinterpret_cast<uintptr_t>(b.const_data_ptr());
    return ap+a.nbytes()<=bp || bp+b.nbytes()<=ap;
}
void record(unsigned operation,const Tensor &input,ScalarType dtype,int64_t zp,uint32_t ticks,bool fast) {
    auto &stats=dtln_quantization_stats[dtln_quantization_current_method<8?dtln_quantization_current_method:8];
    stats.ticks[operation]+=ticks;++stats.calls[operation];
    stats.elements[operation]+=uint32_t(input.numel());
    const unsigned d=unsigned(dtype);
    if(d<32)stats.dtype_mask[operation]|=1U<<d;
    stats.nonzero_points[operation]+=zp!=0;
    ++(fast?stats.fast[operation]:stats.fallback[operation]);
}
}
void dtln_quantize_kernel(KernelRuntimeContext &ctx,Span<EValue *> s) {
    if(s.size()!=8){ctx.fail(Error::InvalidProgram);return;}
    const Tensor &input=s[0]->toTensor();
    const double scale=s[1]->toDouble();
    const int64_t zp=s[2]->toInt(),lo=s[3]->toInt(),hi=s[4]->toInt();
    const auto dtype=s[5]->toScalarType();
    Tensor &out=s[6]->toTensor();
    const uint32_t start=osKernelGetSysTimerCount();
    const float float_scale=static_cast<float>(scale);
    const float inverse_scale=1.0f/float_scale;
    const bool fast=input.scalar_type()==ScalarType::Float && dtype==ScalarType::Short &&
        out.scalar_type()==dtype && zp==0 && lo>=-32768 && hi<=32767 && lo<=hi &&
        finite_positive(float_scale) && finite_positive(inverse_scale) &&
        std::fegetround()==FE_TONEAREST && compatible(input,out) &&
        dtln::kernels::quantize_short(input.const_data_ptr<float>(),
            out.mutable_data_ptr<int16_t>(),input.numel(),inverse_scale,int16_t(lo),int16_t(hi));
    if (!fast) torch::executor::native::quantize_per_tensor_out(ctx,input,scale,zp,lo,hi,dtype,out);
    record(0,input,dtype,zp,osKernelGetSysTimerCount()-start,fast);
}
void dtln_dequantize_kernel(KernelRuntimeContext &ctx,Span<EValue *> s) {
    if(s.size()!=9){ctx.fail(Error::InvalidProgram);return;}
    const Tensor &input=s[0]->toTensor();
    const double scale=s[1]->toDouble();
    const int64_t zp=s[2]->toInt(),lo=s[3]->toInt(),hi=s[4]->toInt();
    const auto dtype=s[5]->toScalarType();
    const auto output_dtype=s[6]->toOptional<ScalarType>();
    Tensor &out=s[7]->toTensor();
    const uint32_t start=osKernelGetSysTimerCount();
    const float float_scale=static_cast<float>(scale);
    const bool fast=DTLN_FAST_DEQUANTIZE && input.scalar_type()==ScalarType::Short && dtype==ScalarType::Short &&
        out.scalar_type()==ScalarType::Float && (!output_dtype || *output_dtype==ScalarType::Float) &&
        zp==0 && lo>=-32768 && hi<=32767 && lo<=hi && finite_positive(float_scale) && compatible(input,out);
    if (fast) dtln::kernels::dequantize_short(input.const_data_ptr<int16_t>(),
        out.mutable_data_ptr<float>(),input.numel(),float_scale);
    else torch::executor::native::dequantize_per_tensor_out(ctx,input,scale,zp,lo,hi,dtype,output_dtype,out);
    record(1,input,dtype,zp,osKernelGetSysTimerCount()-start,fast);
}
