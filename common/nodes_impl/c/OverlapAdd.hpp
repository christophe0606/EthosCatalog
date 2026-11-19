// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "NodeSupport.hpp"

template<typename IN, int ni, typename OUT, int no>
class OverlapAdd : public GenericNode<IN, ni, OUT, no> {
public:
    OverlapAdd(FIFOBase<IN>& input, FIFOBase<OUT>& output)
        : GenericNode<IN, ni, OUT, no>(input, output) {
        static_assert(std::is_same_v<IN, float> && std::is_same_v<OUT, float>);
        static_assert(no > 0 && no <= ni);
    }
    int run() final {
        const auto* input = this->getReadBuffer();
        auto* output = this->getWriteBuffer();
        for (unsigned i = 0; i < ni; ++i) buffer_[i] += input[i];
        std::memcpy(output, buffer_, no * sizeof(float));
        std::memmove(buffer_, buffer_ + no, (ni - no) * sizeof(float));
        std::fill(buffer_ + ni - no, buffer_ + ni, 0.0f);
        return CG_SUCCESS;
    }
private:
    float buffer_[ni]{};
};
