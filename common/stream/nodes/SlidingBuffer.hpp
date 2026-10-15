// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "NodeSupport.hpp"
template<typename IN, int inputSamples, typename OUT, int outputSamples>
class SlidingBuffer : public GenericNode<IN,inputSamples,OUT,outputSamples> {
public:
    SlidingBuffer(FIFOBase<IN>& in, FIFOBase<OUT>& out) : GenericNode<IN,inputSamples,OUT,outputSamples>(in,out) {
        static_assert(inputSamples == 1 && outputSamples == 1 && block_samples<IN> <= block_samples<OUT>);
    }
    cg_status init() final { sliding_buffer_init(&state_, history_, block_samples<OUT>); return CG_SUCCESS; }
    int run() final {
        auto* in = this->getReadBuffer(); auto* out = this->getWriteBuffer();
        if (in->info.discontinuity) sliding_buffer_init(&state_, history_, block_samples<OUT>);
        out->info = in->info;
        out->info.valid = sliding_buffer_push(&state_, in->samples, block_samples<IN>);
        if (out->info.valid) std::memcpy(out->samples, history_, sizeof(history_));
        return CG_SUCCESS;
    }
private:
    sliding_buffer_t state_{};
    float history_[block_samples<OUT>];
};
