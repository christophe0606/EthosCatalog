// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "normalization.hpp"
#include "float_loops.hpp"
#include "quantization.hpp"
#include "arm_embedded_module.hpp"
#include <executorch/runtime/kernel/kernel_runtime_context.h>
#include <executorch/runtime/kernel/operator_registry.h>
#include <optional>
#ifndef DTLN_NORMALIZATION_TIMING
#define DTLN_NORMALIZATION_TIMING 1
#endif
#if DTLN_NORMALIZATION_TIMING
#include "cmsis_os2.h"
#endif

using namespace executorch::runtime;
using namespace executorch::aten;
namespace torch::executor::native {
Tensor &mean_dim_out(KernelRuntimeContext &, const Tensor &,
    std::optional<executorch::aten::ArrayRef<int64_t>>, bool, std::optional<ScalarType>, Tensor &);
Tensor &sub_out(KernelRuntimeContext &, const Tensor &, const Tensor &, const Scalar &, Tensor &);
Tensor &add_out(KernelRuntimeContext &, const Tensor &, const Tensor &, const Scalar &, Tensor &);
Tensor &mul_out(KernelRuntimeContext &, const Tensor &, const Tensor &, Tensor &);
Tensor &pow_Tensor_Scalar_out(KernelRuntimeContext &, const Tensor &, const Scalar &, Tensor &);
Tensor &rsqrt_out(KernelRuntimeContext &, const Tensor &, Tensor &);
}
DtlnKernelStats dtln_kernel_stats{};
DtlnNormalizationStats dtln_normalization_stats[9]{};
namespace {
namespace loops = dtln::kernels;
struct KernelTimer {
#if DTLN_NORMALIZATION_TIMING
    unsigned operation;
    uint32_t start;
    explicit KernelTimer(unsigned op) : operation(op), start(osKernelGetSysTimerCount()) {}
    ~KernelTimer() {
        auto &stats=dtln_normalization_stats[dtln_quantization_current_method<8?dtln_quantization_current_method:8];
        stats.ticks[operation]+=uint32_t(osKernelGetSysTimerCount()-start);
        ++stats.calls[operation];
    }
#else
    explicit KernelTimer(unsigned) {}
#endif
};
bool dense_float(const Tensor &a) {
    if (a.scalar_type() != ScalarType::Float || a.dim() > loops::max_rank || a.numel() <= 0)
        return false;
    int64_t stride = 1;
    for (int d = int(a.dim())-1; d >= 0; --d) {
        if (a.size(d) > 1 && a.strides()[d] != stride) return false;
        stride *= a.size(d);
    }
    return true;
}
loops::Shape shape(const Tensor &a) {
    loops::Shape result{};
    result.rank = int(a.dim());
    for (int d = 0; d < result.rank; ++d) result.extent[d] = a.size(d);
    return result;
}
double scalar_value(const Scalar &s) {
    return s.isFloatingPoint() ? s.to<double>() : double(s.to<int64_t>());
}
bool same_shape(const Tensor &a, const Tensor &b) {
    if (a.dim() != b.dim()) return false;
    for (int d = 0; d < a.dim(); ++d) if (a.size(d) != b.size(d)) return false;
    return true;
}
bool binary_fast(const Tensor &a, const Tensor &b, Tensor &out,
                 loops::Binary op, float alpha) {
    if (!dense_float(a) || !dense_float(b) || !dense_float(out)) return false;
    const auto aa = shape(a), bb = shape(b), oo = shape(out);
    // The output must be the actual broadcast result, not merely a larger
    // compatible destination. Runtime-static tensors need no resize here.
    for (int d = 0; d < oo.rank; ++d) {
        const int ad = d-(oo.rank-aa.rank), bd = d-(oo.rank-bb.rank);
        const int64_t x = ad < 0 ? 1 : aa.extent[ad];
        const int64_t y = bd < 0 ? 1 : bb.extent[bd];
        if ((x > y ? x : y) != oo.extent[d]) return false;
    }
    // Reject an expanded input alias: writing would overwrite a reused scalar
    // or channel vector before the full operation has consumed it.
    float *dest = out.mutable_data_ptr<float>();
    if ((dest == a.const_data_ptr<float>() && a.numel() != out.numel()) ||
        (dest == b.const_data_ptr<float>() && b.numel() != out.numel())) return false;
    if (!loops::repeated_suffix(aa,oo) || !loops::repeated_suffix(bb,oo)) {
        const size_t a_inner=loops::row_broadcast_inner(aa,oo);
        const size_t b_inner=loops::row_broadcast_inner(bb,oo);
        if (same_shape(a,out) && b_inner && b.numel()==out.numel()/b_inner) {
            loops::binary_rows(a.const_data_ptr<float>(),b.const_data_ptr<float>(),
                dest,out.numel(),b_inner,false,op,alpha);
        } else if (same_shape(b,out) && a_inner && a.numel()==out.numel()/a_inner) {
            loops::binary_rows(b.const_data_ptr<float>(),a.const_data_ptr<float>(),
                dest,out.numel(),a_inner,true,op,alpha);
        } else return false;
        return true;
    }
    loops::binary(a.const_data_ptr<float>(), a.numel(), b.const_data_ptr<float>(),
                  b.numel(), dest, out.numel(), op, alpha);
    return true;
}
void binary(KernelRuntimeContext &ctx, Span<EValue *> stack, int index, loops::Binary op) {
    const KernelTimer timer(index);
    const bool multiply = op == loops::Binary::multiply;
    if (stack.size() != (multiply ? 4u : 5u)) { ctx.fail(Error::InvalidProgram); return; }
    const Tensor &a = stack[0]->toTensor(), &b = stack[1]->toTensor();
    Tensor &out = stack[multiply ? 2 : 3]->toTensor();
    const Scalar alpha = multiply ? Scalar(1.0) : stack[2]->toScalar();
    if (!alpha.isBoolean() && binary_fast(a, b, out, op, float(scalar_value(alpha)))) {
        ++dtln_kernel_stats.fast[index]; return;
    }
    ++dtln_kernel_stats.fallback[index];
    if (multiply) torch::executor::native::mul_out(ctx, a, b, out);
    else if (op == loops::Binary::subtract) torch::executor::native::sub_out(ctx, a, b, alpha, out);
    else torch::executor::native::add_out(ctx, a, b, alpha, out);
}
void subtract(KernelRuntimeContext &c, Span<EValue *> s) { binary(c,s,1,loops::Binary::subtract); }
void add(KernelRuntimeContext &c, Span<EValue *> s) { binary(c,s,3,loops::Binary::add); }
void multiply(KernelRuntimeContext &c, Span<EValue *> s) { binary(c,s,5,loops::Binary::multiply); }
bool mean_fast(const Tensor &in, std::optional<executorch::aten::ArrayRef<int64_t>> dims,
               bool keep, std::optional<ScalarType> dtype, Tensor &out) {
    if (!dense_float(in) || !dense_float(out) || (dtype && *dtype != ScalarType::Float))
        return false;
    bool reduce[loops::max_rank]{};
    if (!dims || dims->empty()) {
        for (int d = 0; d < in.dim(); ++d) reduce[d] = true;
    } else {
        for (int64_t axis : *dims) {
            if (axis < 0) axis += in.dim();
            if (axis < 0 || axis >= in.dim() || reduce[axis]) return false;
            reduce[axis] = true;
        }
    }
    bool started = false;
    size_t inner = 1;
    int od = 0;
    for (int d = 0; d < in.dim(); ++d) {
        if (reduce[d]) { started = true; inner *= size_t(in.size(d)); }
        else if (started) return false; // Only contiguous suffix reductions.
        if (keep || !reduce[d]) {
            if (od >= out.dim() || out.size(od++) != (reduce[d] ? 1 : in.size(d))) return false;
        }
    }
    if (od != out.dim() || out.numel() != in.numel()/inner) return false;
    loops::mean_tail(in.const_data_ptr<float>(), out.mutable_data_ptr<float>(), out.numel(), inner);
    return true;
}
void mean(KernelRuntimeContext &ctx, Span<EValue *> s) {
    const KernelTimer timer(0);
    if (s.size() != 6) { ctx.fail(Error::InvalidProgram); return; }
    const Tensor &a = s[0]->toTensor();
    auto dims = s[1]->toOptional<executorch::aten::ArrayRef<int64_t>>();
    const bool keep = s[2]->toBool();
    auto dtype = s[3]->toOptional<ScalarType>();
    Tensor &out = s[4]->toTensor();
    if (mean_fast(a,dims,keep,dtype,out)) { ++dtln_kernel_stats.fast[0]; return; }
    ++dtln_kernel_stats.fallback[0];
    torch::executor::native::mean_dim_out(ctx,a,dims,keep,dtype,out);
}
void square(KernelRuntimeContext &ctx, Span<EValue *> s) {
    const KernelTimer timer(2);
    if (s.size() != 4) { ctx.fail(Error::InvalidProgram); return; }
    const Tensor &a = s[0]->toTensor();
    const Scalar exponent = s[1]->toScalar();
    Tensor &out = s[2]->toTensor();
    if (!exponent.isBoolean() && scalar_value(exponent) == 2.0 &&
        dense_float(a) && dense_float(out) && same_shape(a,out)) {
        loops::square(a.const_data_ptr<float>(),out.mutable_data_ptr<float>(),a.numel());
        ++dtln_kernel_stats.fast[2]; return;
    }
    ++dtln_kernel_stats.fallback[2];
    torch::executor::native::pow_Tensor_Scalar_out(ctx,a,exponent,out);
}
void rsqrt(KernelRuntimeContext &ctx, Span<EValue *> s) {
    const KernelTimer timer(4);
    if (s.size() != 3) { ctx.fail(Error::InvalidProgram); return; }
    const Tensor &a = s[0]->toTensor();
    Tensor &out = s[1]->toTensor();
    if (dense_float(a) && dense_float(out) && same_shape(a,out)) {
        loops::reciprocal_sqrt(a.const_data_ptr<float>(),out.mutable_data_ptr<float>(),a.numel());
        ++dtln_kernel_stats.fast[4]; return;
    }
    ++dtln_kernel_stats.fallback[4];
    torch::executor::native::rsqrt_out(ctx,a,out);
}
const Kernel kernels[] = {
    Kernel("aten::mean.out",mean), Kernel("aten::sub.out",subtract),
    Kernel("aten::pow.Tensor_Scalar_out",square), Kernel("aten::add.out",add),
    Kernel("aten::rsqrt.out",rsqrt), Kernel("aten::mul.out",multiply),
    Kernel("quantized_decomposed::quantize_per_tensor.out",dtln_quantize_kernel),
    Kernel("quantized_decomposed::dequantize_per_tensor.out",dtln_dequantize_kernel)
};
}
int dtln_install_normalization(arm::embedded::EmbeddedModule *module) {
    return module && module->set_kernel_registry({kernels,8}) == Error::Ok ? 0 : -454;
}
