// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdbool.h>
#include <stddef.h>

// Private C backend interface; caller owns the history storage.
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { float *data; size_t size, valid; } sliding_buffer_t;
void sliding_buffer_init(sliding_buffer_t *state, float *storage, size_t size);
bool sliding_buffer_push(sliding_buffer_t *state, const float *block, size_t count);

#ifdef __cplusplus
}
#include "NodeSupport.hpp"

template<typename IN, int inputSamples, typename OUT, int outputSamples>
class SlidingBuffer : public GenericNode<IN,inputSamples,OUT,outputSamples> {
public:
    SlidingBuffer(FIFOBase<IN>& in, FIFOBase<OUT>& out) : GenericNode<IN,inputSamples,OUT,outputSamples>(in,out) {
        static_assert(std::is_same_v<IN,float> && std::is_same_v<OUT,float>);
        static_assert(inputSamples > 0 && inputSamples <= outputSamples);
    }
    cg_status init() final { overlap_.fill(0.0f); return CG_SUCCESS; }
    int run() final {
        auto* in = this->getReadBuffer(); auto* out = this->getWriteBuffer();
        // Copy the new samples first, even if FIFO storage overlaps.
        std::memmove(out + overlapSamples, in, inputSamples * sizeof(float));
        if constexpr (overlapSamples > 0) {
            std::memcpy(out, overlap_.data(), overlapSamples * sizeof(float));
            // Downstream nodes may overwrite the output; retain only the tail
            // needed by the next firing, including zero padding during startup.
            std::memcpy(overlap_.data(), out + inputSamples, overlapSamples * sizeof(float));
        }
        return CG_SUCCESS;
    }
private:
    static constexpr int overlapSamples = outputSamples - inputSamples;
    std::array<float, overlapSamples> overlap_;
};
#endif // __cplusplus
