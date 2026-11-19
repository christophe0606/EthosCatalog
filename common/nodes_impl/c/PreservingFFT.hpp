// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "NodeSupport.hpp"
#include "arm_math.h"

template<typename IN, int ni, typename OUT, int no>
class PreservingFFT : public GenericNode<IN, ni, OUT, no> {
public:
    PreservingFFT(FIFOBase<IN>& input, FIFOBase<OUT>& output)
        : GenericNode<IN, ni, OUT, no>(input, output) {
        static_assert(std::is_same_v<IN, float> && std::is_same_v<OUT, float>);
        static_assert(ni == 512 && no == ni);
    }
    cg_status init() final {
        return arm_rfft_fast_init_512_f32(&fft_) == ARM_MATH_SUCCESS ?
            CG_SUCCESS : CG_INIT_FAILURE;
    }
    int run() final {
        std::memcpy(input_, this->getReadBuffer(), sizeof(input_));
        // CMSIS-DSP mutates input, while another branch may retain the FIFO.
        arm_rfft_fast_f32(&fft_, input_, this->getWriteBuffer(), 0);
        return CG_SUCCESS;
    }
private:
    arm_rfft_fast_instance_f32 fft_{};
    float input_[ni]{};
};
