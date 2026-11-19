// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "NodeSupport.hpp"
#include <cmath>

template<typename IN, int ni, typename OUT, int no>
class RawRFFTMagnitude : public GenericNode<IN, ni, OUT, no> {
public:
    RawRFFTMagnitude(FIFOBase<IN>& input, FIFOBase<OUT>& output)
        : GenericNode<IN, ni, OUT, no>(input, output) {
        static_assert(std::is_same_v<IN, float> && std::is_same_v<OUT, float>);
        static_assert(ni >= 4 && ni % 2 == 0 && no == ni / 2 + 1);
    }
    int run() final {
        const auto* input = this->getReadBuffer();
        auto* output = this->getWriteBuffer();
        output[0] = std::fabs(input[0]);
        output[ni / 2] = std::fabs(input[1]);
        for (unsigned bin = 1; bin < ni / 2; ++bin) {
            const float real = input[2 * bin], imag = input[2 * bin + 1];
            output[bin] = std::sqrt(real * real + imag * imag);
        }
        return CG_SUCCESS;
    }
};
