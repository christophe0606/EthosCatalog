// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "embedded_yamnet.h"
#include "cmsis_os2.h"
#include "embedded_runtime.hpp"
#include <array>
#include <cstring>
int yamnet_forward(void *opaque, float *features, float *scores, uint32_t *ticks) {
    using namespace executorch::aten;
    if (!opaque || !features || !scores)
        return -400;
    auto *module = static_cast<arm::embedded::EmbeddedModule *>(opaque);
    std::array<SizesType, 3> shape{1, 96, 64};
    std::array<DimOrderType, 3> order{0, 1, 2};
    TensorImpl impl(ScalarType::Float, 3, shape.data(), features, order.data());
    uint32_t start = osKernelGetSysTimerCount();
    auto result = module->forward(Tensor(&impl));
    if (ticks)
        *ticks = osKernelGetSysTimerCount() - start;
    if (!result.ok() || result->size() != 1 || !result->front().isTensor())
        return -401;
    Tensor out = result->front().toTensor();
    if (out.scalar_type() != ScalarType::Float || out.numel() != 521)
        return -402;
    std::memcpy(scores, out.const_data_ptr<float>(), 521 * sizeof(float));
    return 0;
}
