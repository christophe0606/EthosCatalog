// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "cpu_kernels.hpp"

#include "arm_embedded_module.hpp"
#include "cpu_loops.hpp"
#include "elu.hpp"
#include "model_pte.h"
#ifdef ENCODEC_FAST_RECURRENT
#include "recurrent.hpp"
#endif
#include "transpose.hpp"
#include <executorch/runtime/kernel/kernel_runtime_context.h>
#include <executorch/runtime/kernel/operator_registry.h>

#include <cfenv>
#include <cstring>
#include <optional>

#ifdef ENCODEC_CPU_PROFILING
#include "../codec_runtime.hpp"
#include "cmsis_os2.h"
#include <array>
#include <iterator>
#include <utility>
#endif

using namespace executorch::aten;
using namespace executorch::runtime;

namespace torch::executor::native {
Tensor& add_out(KernelRuntimeContext&, const Tensor&, const Tensor&, const Scalar&, Tensor&);
Tensor& sub_out(KernelRuntimeContext&, const Tensor&, const Tensor&, const Scalar&, Tensor&);
Tensor& mul_out(KernelRuntimeContext&, const Tensor&, const Tensor&, Tensor&);
Tensor& where_out(KernelRuntimeContext&, const Tensor&, const Tensor&, const Tensor&, Tensor&);
Tensor& argmax_out(KernelRuntimeContext&, const Tensor&, std::optional<int64_t>, bool, Tensor&);
Tensor& neg_out(KernelRuntimeContext&, const Tensor&, Tensor&);
Tensor& pow_Tensor_Scalar_out(KernelRuntimeContext&, const Tensor&, const Scalar&, Tensor&);
Tensor& permute_copy_out(KernelRuntimeContext&, const Tensor&, IntArrayRef, Tensor&);
Tensor& elu_out(KernelRuntimeContext&, const Tensor&, const Scalar&, const Scalar&, const Scalar&, Tensor&);
Tensor& sigmoid_out(KernelRuntimeContext&, const Tensor&, Tensor&);
Tensor& tanh_out(KernelRuntimeContext&, const Tensor&, Tensor&);
Tensor& quantize_per_tensor_out(KernelRuntimeContext&, const Tensor&, double, int64_t, int64_t,
                               int64_t, ScalarType, Tensor&);
}

namespace {
using encodec::cpu::Binary;
namespace native = torch::executor::native;

bool dense(const Tensor& tensor)
{
    if (tensor.numel() <= 0 || !tensor.const_data_ptr()) return false;
    const auto sizes = tensor.sizes();
    const auto strides = tensor.strides();
    const auto order = tensor.dim_order();
    std::size_t stride = 1;
    for (std::size_t dim = sizes.size(); dim > 0;) {
        --dim;
        if (order[dim] != dim || (sizes[dim] > 1 && strides[dim] != static_cast<int64_t>(stride))) {
            return false;
        }
        stride *= sizes[dim];
    }
    return true;
}

bool sameShape(const Tensor& a, const Tensor& b)
{
    return a.sizes().equals(b.sizes());
}

bool disjoint(const Tensor& input, const Tensor& output)
{
    const auto source = reinterpret_cast<uintptr_t>(input.const_data_ptr());
    const auto target = reinterpret_cast<uintptr_t>(output.const_data_ptr());
    if (source <= target) return target - source >= input.nbytes();
    return source - target >= output.nbytes();
}

bool noDestructiveOverlap(const Tensor& input, const Tensor& output)
{
    return input.const_data_ptr() == output.const_data_ptr() || disjoint(input, output);
}

bool repeatedSuffix(const Tensor& operand, const Tensor& out)
{
    const auto sizes = operand.sizes();
    const auto finalSizes = out.sizes();
    bool started = false;
    const std::size_t leading = finalSizes.size() - sizes.size();
    for (std::size_t dim = 0; dim < sizes.size(); ++dim) {
        started |= sizes[dim] != 1;
        if (started && sizes[dim] != finalSizes[leading + dim]) return false;
    }
    return true;
}

bool lastAxisBroadcast(const Tensor& operand, const Tensor& out)
{
    const auto sizes = operand.sizes();
    const auto finalSizes = out.sizes();
    if (sizes.size() != finalSizes.size() || sizes.empty() || sizes.back() != 1) return false;
    for (std::size_t dim = 0; dim + 1 < sizes.size(); ++dim) {
        if (sizes[dim] != finalSizes[dim]) return false;
    }
    return true;
}

// The demo and benchmark dispatch through one serial codec workspace.
alignas(32) float reusedFlashValues[1024];

const float* cacheRepeatedOperand(const float* values, std::size_t bytes)
{
    const auto base = reinterpret_cast<uintptr_t>(model_pte);
    const auto address = reinterpret_cast<uintptr_t>(values);
    if (address >= base && bytes <= sizeof(reusedFlashValues) && bytes <= model_pte_size &&
        address - base <= model_pte_size - bytes) {
        std::memcpy(reusedFlashValues, values, bytes);
        return reusedFlashValues;
    }
    return values;
}

template<Binary Op, bool Swap>
bool broadcast(const Tensor& full, const Tensor& operand, Tensor& out)
{
    if (operand.dim() > out.dim() || !noDestructiveOverlap(full, out) || !disjoint(operand, out)) {
        return false;
    }
    const auto* x = full.const_data_ptr<float>();
    const auto* y = operand.const_data_ptr<float>();
    auto* dst = out.mutable_data_ptr<float>();
    const std::size_t count = out.numel();
    if (operand.numel() == 1) {
        encodec::cpu::binaryLoop<Op, Swap, true>(x, y, dst, count);
    } else if (repeatedSuffix(operand, out)) {
        const std::size_t suffix = operand.numel();
        y = cacheRepeatedOperand(y, operand.nbytes());
        for (std::size_t start = 0; start < count; start += suffix) {
            encodec::cpu::binaryLoop<Op, Swap>(x + start, y, dst + start, suffix);
        }
    } else if (lastAxisBroadcast(operand, out)) {
        const std::size_t row = out.sizes().back();
        for (std::size_t start = 0; start < count; start += row, ++y) {
            encodec::cpu::binaryLoop<Op, Swap, true>(x + start, y, dst + start, row);
        }
    } else {
        return false;
    }
    return true;
}

template<Binary Op>
bool binaryFast(const Tensor& a, const Tensor& b, Tensor& out)
{
    if (a.scalar_type() != ScalarType::Float || b.scalar_type() != ScalarType::Float ||
        out.scalar_type() != ScalarType::Float || !dense(a) || !dense(b) || !dense(out)) {
        return false;
    }
    const bool fullA = sameShape(a, out);
    const bool fullB = sameShape(b, out);
    if (fullA && fullB) {
        if (!noDestructiveOverlap(a, out) || !noDestructiveOverlap(b, out)) return false;
        encodec::cpu::binaryLoop<Op>(a.const_data_ptr<float>(), b.const_data_ptr<float>(),
                                     out.mutable_data_ptr<float>(), out.numel());
        return true;
    }
    if (fullA) return broadcast<Op, false>(a, b, out);
    if (fullB) return broadcast<Op, true>(b, a, out);
    return false;
}

bool isOne(const Scalar& scalar)
{
    if (scalar.isFloatingPoint()) return scalar.to<double>() == 1.0;
    return scalar.isIntegral(false) && scalar.to<int64_t>() == 1;
}

template<Binary Op>
void binaryKernel(KernelRuntimeContext& context, Span<EValue*> stack)
{
    constexpr bool multiply = Op == Binary::multiply;
    if (stack.size() != (multiply ? 4U : 5U)) {
        context.fail(Error::InvalidProgram);
        return;
    }
    const Tensor& a = stack[0]->toTensor();
    const Tensor& b = stack[1]->toTensor();
    Tensor& out = stack[multiply ? 2 : 3]->toTensor();
    if constexpr (multiply) {
        if (!binaryFast<Op>(a, b, out)) native::mul_out(context, a, b, out);
    } else {
        const Scalar alpha = stack[2]->toScalar();
        if (isOne(alpha) && binaryFast<Op>(a, b, out)) return;
        if constexpr (Op == Binary::add) native::add_out(context, a, b, alpha, out);
        else native::sub_out(context, a, b, alpha, out);
    }
}

void whereKernel(KernelRuntimeContext& context, Span<EValue*> stack)
{
    if (stack.size() != 5) {
        context.fail(Error::InvalidProgram);
        return;
    }
    const Tensor& condition = stack[0]->toTensor();
    const Tensor& a = stack[1]->toTensor();
    const Tensor& b = stack[2]->toTensor();
    Tensor& out = stack[3]->toTensor();
    if (condition.scalar_type() == ScalarType::Bool && condition.numel() == 1 &&
        condition.dim() <= out.dim() && dense(condition) && a.scalar_type() == ScalarType::Float &&
        b.scalar_type() == ScalarType::Float && out.scalar_type() == ScalarType::Float &&
        sameShape(a, out) && sameShape(b, out) && dense(a) && dense(b) && dense(out)) {
        const auto& chosen = *condition.const_data_ptr<bool>() ? a : b;
        std::memmove(out.mutable_data_ptr(), chosen.const_data_ptr(), out.nbytes());
        return;
    }
    native::where_out(context, condition, a, b, out);
}

void argmaxKernel(KernelRuntimeContext& context, Span<EValue*> stack)
{
    if (stack.size() != 5) {
        context.fail(Error::InvalidProgram);
        return;
    }
    const auto& in = stack[0]->toTensor();
    const auto dim = stack[1]->toOptional<int64_t>();
    const bool keepdim = stack[2]->toBool();
    auto& out = stack[3]->toTensor();
    if (in.dim() == 3 && out.dim() == 2 && dim && (*dim == -1 || *dim == 2) && !keepdim &&
        in.scalar_type() == ScalarType::Float && out.scalar_type() == ScalarType::Long &&
        in.size(0) == out.size(0) && in.size(1) == out.size(1) &&
        dense(in) && dense(out) && disjoint(in, out)) {
        const float* input = in.const_data_ptr<float>();
        auto* output = out.mutable_data_ptr<int64_t>();
        const std::size_t width = in.size(2);
        const std::size_t rows = out.numel();
        for (std::size_t row = 0; row < rows; ++row) {
            output[row] = encodec::cpu::argmaxRow(input + row * width, width);
        }
        return;
    }
    native::argmax_out(context, in, dim, keepdim, out);
}

bool denseFloatUnary(const Tensor& input, const Tensor& output)
{
    return input.scalar_type() == ScalarType::Float && output.scalar_type() == ScalarType::Float &&
           sameShape(input, output) && dense(input) && dense(output) && noDestructiveOverlap(input, output);
}

void negKernel(KernelRuntimeContext& context, Span<EValue*> stack)
{
    if (stack.size() != 3) {
        context.fail(Error::InvalidProgram);
        return;
    }
    const auto& input = stack[0]->toTensor();
    auto& output = stack[1]->toTensor();
    if (denseFloatUnary(input, output)) {
        const auto* src = input.const_data_ptr<float>();
        auto* dst = output.mutable_data_ptr<float>();
        const std::size_t count = input.numel();
        for (std::size_t i = 0; i < count; ++i) dst[i] = -src[i];
        return;
    }
    native::neg_out(context, input, output);
}

void powKernel(KernelRuntimeContext& context, Span<EValue*> stack)
{
    if (stack.size() != 4) {
        context.fail(Error::InvalidProgram);
        return;
    }
    const auto& input = stack[0]->toTensor();
    const auto exponent = stack[1]->toScalar();
    auto& output = stack[2]->toTensor();
    if (exponent.isIntegral(false) && exponent.to<int64_t>() == 2 && denseFloatUnary(input, output)) {
        const auto* src = input.const_data_ptr<float>();
        auto* dst = output.mutable_data_ptr<float>();
        const std::size_t count = input.numel();
        for (std::size_t i = 0; i < count; ++i) dst[i] = src[i] * src[i];
        return;
    }
    native::pow_Tensor_Scalar_out(context, input, exponent, output);
}

bool isLastTwoSwap(const Tensor& input, IntArrayRef dims, const Tensor& output)
{
    const std::size_t rank = input.dim();
    if (rank < 2 || rank != output.dim() || rank != dims.size()) return false;
    for (std::size_t i = 0; i < rank; ++i) {
        const auto axis = i < rank - 2 ? i : (i == rank - 2 ? rank - 1 : rank - 2);
        const int64_t order = dims[i] < 0 ? dims[i] + static_cast<int64_t>(rank) : dims[i];
        if (order != static_cast<int64_t>(axis) || input.size(axis) != output.size(i)) return false;
    }
    return true;
}

void permuteKernel(KernelRuntimeContext& context, Span<EValue*> stack)
{
    if (stack.size() != 4) {
        context.fail(Error::InvalidProgram);
        return;
    }
    const auto& input = stack[0]->toTensor();
    const auto dims = stack[1]->toIntList();
    auto& output = stack[2]->toTensor();
    if (input.scalar_type() == ScalarType::Float && output.scalar_type() == ScalarType::Float &&
        isLastTwoSwap(input, dims, output) && dense(input) && dense(output) && disjoint(input, output)) {
        const std::size_t rank = input.dim();
        const std::size_t rows = input.size(rank - 2);
        const std::size_t columns = input.size(rank - 1);
        const std::size_t batch = input.numel() / (rows * columns);
        encodec::cpu::transposeLastTwo(input.const_data_ptr<float>(), output.mutable_data_ptr<float>(),
                                       batch, rows, columns);
        return;
    }
    native::permute_copy_out(context, input, dims, output);
}

encodec::cpu::EluTable eluTable;

void eluKernel(KernelRuntimeContext& context, Span<EValue*> stack)
{
    if (stack.size() != 6) {
        context.fail(Error::InvalidProgram);
        return;
    }
    const auto& input = stack[0]->toTensor();
    const auto alpha = stack[1]->toScalar();
    const auto scale = stack[2]->toScalar();
    const auto inputScale = stack[3]->toScalar();
    auto& output = stack[4]->toTensor();
    if (isOne(alpha) && isOne(scale) && isOne(inputScale) && denseFloatUnary(input, output)) {
        encodec::cpu::approximateElu(input.const_data_ptr<float>(), output.mutable_data_ptr<float>(),
                                    input.numel(), eluTable);
        return;
    }
    native::elu_out(context, input, alpha, scale, inputScale, output);
}

#ifdef ENCODEC_FAST_RECURRENT
using encodec::cpu::Gate;
template<Gate G>
alignas(16) encodec::cpu::GateTable<G> gateTable;

template<Gate G>
void recurrentKernel(KernelRuntimeContext& context, Span<EValue*> stack)
{
    if (stack.size() != 3) {
        context.fail(Error::InvalidProgram);
        return;
    }
    const auto& input = stack[0]->toTensor();
    auto& output = stack[1]->toTensor();
    if (denseFloatUnary(input, output)) {
        encodec::cpu::approximateGate<G>(input.const_data_ptr<float>(), output.mutable_data_ptr<float>(),
                                        input.numel(), gateTable<G>);
        return;
    }
    if constexpr (G == Gate::sigmoid) {
        native::sigmoid_out(context, input, output);
    } else {
        native::tanh_out(context, input, output);
    }
}
#endif

bool finitePositive(float value)
{
    const auto bits = encodec::cpu::floatBits(value);
    return bits != 0 && bits < 0x7f800000U;
}

void quantizeKernel(KernelRuntimeContext& context, Span<EValue*> stack)
{
    if (stack.size() != 8) {
        context.fail(Error::InvalidProgram);
        return;
    }
    const Tensor& input = stack[0]->toTensor();
    const double scale = stack[1]->toDouble();
    const int64_t zero = stack[2]->toInt();
    const int64_t low = stack[3]->toInt();
    const int64_t high = stack[4]->toInt();
    const auto dtype = stack[5]->toScalarType();
    Tensor& out = stack[6]->toTensor();
    const auto scale32 = static_cast<float>(scale);
    if (input.scalar_type() == ScalarType::Float && dtype == ScalarType::Short && out.scalar_type() == dtype &&
        zero == 0 && low >= -32768 && high <= 32767 && low <= high && finitePositive(scale32) &&
        std::fegetround() == FE_TONEAREST && sameShape(input, out) && dense(input) && dense(out) &&
        disjoint(input, out)) {
        const float inverse = 1.0f / scale32;
        if (finitePositive(inverse) &&
            encodec::cpu::quantizeShort(input.const_data_ptr<float>(), out.mutable_data_ptr<int16_t>(),
                                        input.numel(), inverse, static_cast<int16_t>(low), static_cast<int16_t>(high))) {
            return;
        }
    }
    native::quantize_per_tensor_out(context, input, scale, zero, low, high, dtype, out);
}

constexpr Kernel kernels[]{
    Kernel("aten::add.out", binaryKernel<Binary::add>),
    Kernel("aten::sub.out", binaryKernel<Binary::subtract>),
    Kernel("aten::mul.out", binaryKernel<Binary::multiply>),
    Kernel("aten::where.self_out", whereKernel),
    Kernel("aten::argmax.out", argmaxKernel),
    Kernel("quantized_decomposed::quantize_per_tensor.out", quantizeKernel),
    Kernel("aten::neg.out", negKernel),
    Kernel("aten::pow.Tensor_Scalar_out", powKernel),
    Kernel("aten::permute_copy.out", permuteKernel),
    Kernel("aten::elu.out", eluKernel),
#ifdef ENCODEC_FAST_RECURRENT
    Kernel("aten::sigmoid.out", recurrentKernel<Gate::sigmoid>),
    Kernel("aten::tanh.out", recurrentKernel<Gate::tanh>),
#endif
};

#ifdef ENCODEC_CPU_PROFILING
constexpr const char* portableNames[]{
    "aten::cat.out",
    "aten::embedding.out",
    "aten::flip.out",
    "aten::full_like.out",
#ifndef ENCODEC_FAST_RECURRENT
    "aten::sigmoid.out",
#endif
    "aten::slice_copy.Tensor_out",
    "aten::split_with_sizes_copy.out",
    "aten::sum.IntList_out",
#ifndef ENCODEC_FAST_RECURRENT
    "aten::tanh.out",
#endif
    "aten::unsqueeze_copy.out",
    "dim_order_ops::_clone_dim_order.out",
    "dim_order_ops::_to_dim_order_copy.out",
    "quantized_decomposed::dequantize_per_tensor.out",
};
constexpr std::size_t overriddenCount = std::size(kernels);
constexpr std::size_t kernelCount = overriddenCount + std::size(portableNames);
std::array<OpFunction, kernelCount> profiledTargets;
std::array<Kernel, kernelCount> profiledKernels;

constexpr const char* profiledName(std::size_t index)
{
    return index < overriddenCount ? kernels[index].name_ : portableNames[index - overriddenCount];
}
#endif

}

#ifdef ENCODEC_CPU_PROFILING
extern "C" {
uint64_t encodecKernelTicks[encodec::methodCount][kernelCount];
uint32_t encodecKernelCalls[encodec::methodCount][kernelCount];
uint64_t encodecBackendCopyTicks[encodec::methodCount];
uint32_t encodecBackendCopyBytes[encodec::methodCount];

void arm_ethos_io_memcpy(void* dst, const void* src, size_t size)
{
    const unsigned method = encodec::profileMethod();
    const uint32_t begin = osKernelGetSysTimerCount();
    std::memcpy(dst, src, size);
    const uint32_t ticks = osKernelGetSysTimerCount() - begin;
    if (method < encodec::methodCount) {
        encodecBackendCopyTicks[method] += ticks;
        encodecBackendCopyBytes[method] += size;
    }
}
}

namespace {
template<std::size_t Index>
void profileKernel(KernelRuntimeContext& context, Span<EValue*> stack)
{
    const unsigned method = encodec::profileMethod();
    const uint32_t begin = osKernelGetSysTimerCount();
    profiledTargets[Index](context, stack);
    const uint32_t ticks = osKernelGetSysTimerCount() - begin;
    if (method < encodec::methodCount) {
        encodecKernelTicks[method][Index] += ticks;
        ++encodecKernelCalls[method][Index];
    }
}

template<std::size_t... Index>
constexpr auto profiledFunctions(std::index_sequence<Index...>)
{
    return std::array<OpFunction, sizeof...(Index)>{profileKernel<Index>...};
}

Span<const Kernel> installProfiling()
{
    constexpr auto functions = profiledFunctions(std::make_index_sequence<kernelCount>{});
    std::size_t count = 0;
    for (std::size_t i = 0; i < kernelCount; ++i) {
        const char* name = profiledName(i);
        if (i < overriddenCount) {
            profiledTargets[i] = kernels[i].op_;
        } else {
            // Keep the runtime's specialized dispatch when it has a dtype or
            // layout key for this name. Profile ordinary portable fallbacks.
            bool specialized = false;
            for (const auto& registered : get_registered_kernels()) {
                if (!registered.kernel_key_.is_fallback() && std::strcmp(name, registered.name_) == 0) {
                    specialized = true;
                    break;
                }
            }
            if (specialized || !registry_has_op_function(name)) continue;
            const auto target = get_op_function_from_registry(name);
            if (!target.ok()) continue;
            profiledTargets[i] = *target;
        }
        profiledKernels[count++] = Kernel(name, functions[i]);
    }
    return {profiledKernels.data(), count};
}
}
#endif

executorch::runtime::Error encodec::installCpuKernels(arm::embedded::EmbeddedModule& module)
{
    encodec::cpu::fillEluTable(eluTable);
#ifdef ENCODEC_FAST_RECURRENT
    encodec::cpu::fillGateTable<Gate::sigmoid>(gateTable<Gate::sigmoid>);
    encodec::cpu::fillGateTable<Gate::tanh>(gateTable<Gate::tanh>);
#endif
#ifdef ENCODEC_CPU_PROFILING
    return module.set_kernel_registry(installProfiling());
#else
    return module.set_kernel_registry({kernels, sizeof(kernels) / sizeof(kernels[0])});
#endif
}
