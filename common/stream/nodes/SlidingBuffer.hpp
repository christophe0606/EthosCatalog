// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "NodeSupport.hpp"

template<typename IN, int inputSamples, typename OUT, int outputSamples>
class SlidingBuffer : public GenericNode<IN,inputSamples,OUT,outputSamples> {
public:
    SlidingBuffer(FIFOBase<IN>& in, FIFOBase<OUT>& out) : GenericNode<IN,inputSamples,OUT,outputSamples>(in,out) {
        static_assert(std::is_same_v<IN,float> && std::is_same_v<OUT,float>);
        static_assert(inputSamples > 0 && inputSamples <= outputSamples);
    }
    cg_status init() final { sliding_buffer_init(&state_, history_, outputSamples); return CG_SUCCESS; }
    int run() final {
        auto* in = this->getReadBuffer(); auto* out = this->getWriteBuffer();
        sliding_buffer_push(&state_, in, inputSamples);
        // History starts with zeros; every firing produces a complete frame.
        std::memcpy(out, history_, sizeof(history_));
        return CG_SUCCESS;
    }
private:
    sliding_buffer_t state_{};
    float history_[outputSamples];
};
