// SPDX-License-Identifier: Apache-2.0
#ifndef MFCC_BACKEND_INTERFACE_H
#define MFCC_BACKEND_INTERFACE_H
#include "arm_math.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct mfcc_config {
    uint32_t frame_samples, fft_length, mel_filters, features;
    const float *window, *filter_coefficients, *dct;
    const uint32_t *filter_positions, *filter_lengths;
    float log_floor;
} mfcc_config_t;
// Private backend. All working storage is owned by the calling node.
int mfcc_init(const mfcc_config_t *config, arm_rfft_fast_instance_f32 *fft);
void mfcc_compute(const mfcc_config_t *config, const arm_rfft_fast_instance_f32 *fft,
                  const float *input, float *frame, float *spectrum, float *mel, float *output);
#ifdef __cplusplus
}
#endif
#endif
#if defined(__cplusplus) && !defined(MFCC_BACKEND_ONLY) && !defined(MFCC_STREAM_WRAPPER_H)
#define MFCC_STREAM_WRAPPER_H
#include "NodeSupport.hpp"
template<typename IN, int inputSamples, typename OUT, int outputSamples>
class MFCC : public GenericNode<IN,inputSamples,OUT,outputSamples> {
public:
    MFCC(FIFOBase<IN>& in, FIFOBase<OUT>& out, const mfcc_config_t& config)
        : GenericNode<IN,inputSamples,OUT,outputSamples>(in,out), config_(config) {
        static_assert(std::is_same_v<IN,float> && std::is_same_v<OUT,float>);
    }
    cg_status init() final {
        if (config_.frame_samples != inputSamples || config_.features != outputSamples ||
            mfcc_init(&config_, &fft_)) return CG_INIT_FAILURE;
        scratch_.reset(new (std::nothrow) float[2*config_.fft_length+2+config_.mel_filters]);
        return scratch_ ? CG_SUCCESS : CG_MEMORY_ALLOCATION_FAILURE;
    }
    int run() final {
        auto* in = this->getReadBuffer();
        auto* out = this->getWriteBuffer();
        float *frame = scratch_.get(), *spectrum = frame+config_.fft_length;
        mfcc_compute(&config_, &fft_, in, frame, spectrum, spectrum+config_.fft_length+2, out);
        return CG_SUCCESS;
    }
private:
    const mfcc_config_t& config_;
    arm_rfft_fast_instance_f32 fft_{};
    std::unique_ptr<float[]> scratch_;
};
#endif
