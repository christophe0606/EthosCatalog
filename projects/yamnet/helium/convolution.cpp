// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "convolution.h"
#include "conv3x3.h"
#include "conv3x3_check.h"
#include "cmsis_os2.h"
#include <executorch/runtime/kernel/kernel_runtime_context.h>
#include <executorch/runtime/kernel/operator_registry.h>
#include <optional>

#if !YAMNET_MVE_F32
#error YAMNet firmware requires floating-point MVE on Cortex-M55
#endif

using namespace executorch::runtime;
using namespace executorch::aten;
namespace torch::executor::native {
Tensor &convolution_out(KernelRuntimeContext &, const Tensor &, const Tensor &,
    const std::optional<Tensor> &, IntArrayRef, IntArrayRef, IntArrayRef,
    bool, IntArrayRef, int64_t, Tensor &);
}
YAMNetConvStats yamnet_conv_stats{};
namespace {
bool pair_is(IntArrayRef a, int value) {
    return a.size() == 2 && a[0] == value && a[1] == value;
}
bool optimized(const Tensor &in, const Tensor &weight, const std::optional<Tensor> &bias,
               IntArrayRef stride, IntArrayRef padding, IntArrayRef dilation,
               bool transposed, IntArrayRef output_padding, int64_t groups, Tensor &out) {
    if (in.scalar_type() != ScalarType::Float || weight.scalar_type() != ScalarType::Float ||
        out.scalar_type() != ScalarType::Float || in.dim() != 4 || weight.dim() != 4 ||
        out.dim() != 4 || in.size(0) != 1 || out.size(0) != 1 || transposed ||
        !pair_is(padding, 0) || !pair_is(dilation, 1) || !pair_is(output_padding, 0) ||
        stride.size() != 2 || stride[0] < 1 || stride[0] > 2 || stride[1] < 1 || stride[1] > 2 ||
        weight.size(1) != 1 || weight.size(2) != 3 || weight.size(3) != 3 ||
        in.size(2) < 3 || in.size(3) < 3 || out.size(1) != weight.size(0) || out.size(1) < 1 ||
        out.size(2) != (in.size(2) - 3) / stride[0] + 1 ||
        out.size(3) != (in.size(3) - 3) / stride[1] + 1)
        return false;
    const bool depthwise = groups == in.size(1) && out.size(1) == in.size(1);
    if (!depthwise && !(groups == 1 && in.size(1) == 1)) return false;
    if (bias && (bias->scalar_type() != ScalarType::Float || bias->dim() != 1 ||
                 bias->size(0) != out.size(1))) return false;
    for (int d = 0; d < 4; ++d)
        if (in.strides()[d] <= 0 || out.strides()[d] <= 0 || weight.strides()[d] <= 0)
            return false;
    const float *source = in.const_data_ptr<float>();
    const float *weights = weight.const_data_ptr<float>();
    float *dest = out.mutable_data_ptr<float>();
    const float *biases = bias ? bias->const_data_ptr<float>() : nullptr;
    for (int channel = 0; channel < out.size(1); ++channel) {
        yamnet::ConvPlane p{
            source + (depthwise ? channel * in.strides()[1] : 0),
            dest + channel * out.strides()[1], weights + channel * weight.strides()[0],
            in.strides()[2], in.strides()[3], out.strides()[2], out.strides()[3],
            weight.strides()[2], weight.strides()[3], out.size(2), out.size(3),
            int(stride[0]), int(stride[1]), biases ? biases[channel * bias->strides()[0]] : 0
        };
        yamnet::conv3x3(p);
    }
    return true;
}
void convolution(KernelRuntimeContext &ctx, Span<EValue *> stack) {
    if (stack.size() != 11) { ctx.fail(Error::InvalidProgram); return; }
    const Tensor &in = stack[0]->toTensor();
    const Tensor &weight = stack[1]->toTensor();
    auto bias = stack[2]->toOptional<Tensor>();
    auto stride = stack[3]->toIntList();
    auto padding = stack[4]->toIntList();
    auto dilation = stack[5]->toIntList();
    bool transposed = stack[6]->toBool();
    auto output_padding = stack[7]->toIntList();
    int64_t groups = stack[8]->toInt();
    Tensor &out = stack[9]->toTensor();
    uint32_t start = osKernelGetSysTimerCount();
    ++yamnet_conv_stats.calls;
    if (optimized(in, weight, bias, stride, padding, dilation, transposed, output_padding, groups, out))
        ++yamnet_conv_stats.vector_calls;
    else {
        ++yamnet_conv_stats.fallback_calls;
        torch::executor::native::convolution_out(ctx, in, weight, bias, stride, padding, dilation,
                                                transposed, output_padding, groups, out);
    }
    yamnet_conv_stats.ticks += osKernelGetSysTimerCount() - start;
}
const Kernel kernels[] = {Kernel("aten::convolution.out", convolution)};
}
int yamnet_install_helium(arm::embedded::EmbeddedModule *module) {
    if (!yamnet::check_conv3x3()) return -435;
    return module && module->set_kernel_registry({kernels, 1}) == Error::Ok ? 0 : -434;
}
