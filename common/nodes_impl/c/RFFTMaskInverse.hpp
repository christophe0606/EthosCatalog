// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "NodeSupport.hpp"
#include "arm_math.h"

template<typename IN, int ni, typename MASK, int nm, typename OUT, int no>
class RFFTMaskInverse : public GenericNode21<IN, ni, MASK, nm, OUT, no> {
public:
    RFFTMaskInverse(FIFOBase<IN>& input, FIFOBase<MASK>& mask, FIFOBase<OUT>& output)
        : GenericNode21<IN, ni, MASK, nm, OUT, no>(input, mask, output) {
        static_assert(std::is_same_v<IN, float> && std::is_same_v<MASK, float> &&
                      std::is_same_v<OUT, float>);
        static_assert(ni == 512 && nm == ni / 2 + 1 && no == ni);
    }
    cg_status init() final {
        return arm_rfft_fast_init_512_f32(&fft_) == ARM_MATH_SUCCESS ?
            CG_SUCCESS : CG_INIT_FAILURE;
    }
    int run() final {
        const auto* input = this->getReadBuffer1();
        const auto* mask = this->getReadBuffer2();
        auto* output = this->getWriteBuffer();
        packed_[0] = input[0] * mask[0];
        packed_[1] = input[1] * mask[ni / 2];
        for (unsigned bin = 1; bin < ni / 2; ++bin) {
            packed_[2 * bin] = input[2 * bin] * mask[bin];
            packed_[2 * bin + 1] = input[2 * bin + 1] * mask[bin];
        }
        // CMSIS-DSP inverse fast RFFT includes the 1/N normalization.
        arm_rfft_fast_f32(&fft_, packed_, output, 1);
        return CG_SUCCESS;
    }
private:
    arm_rfft_fast_instance_f32 fft_{};
    float packed_[ni]{};
};
