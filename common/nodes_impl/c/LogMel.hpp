// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#ifndef LOG_MEL_BACKEND_H
#define LOG_MEL_BACKEND_H
#include "arm_math.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct log_mel_config {
    unsigned frame_samples, fft_length, bands;
    const float *window, *weights;
    float offset;
} log_mel_config_t;
int log_mel_init(const log_mel_config_t *, arm_rfft_fast_instance_f32 *);
void log_mel_compute(const log_mel_config_t *, const arm_rfft_fast_instance_f32 *, const float *,
                     float *, float *, float *);
#ifdef __cplusplus
}
#endif
#endif
#if defined(__cplusplus) && !defined(LOG_MEL_BACKEND_ONLY) && !defined(LOG_MEL_WRAPPER_H)
#define LOG_MEL_WRAPPER_H
#include "NodeSupport.hpp"
template <typename IN, int inputSamples, typename OUT, int outputSamples>
class LogMel : public GenericNode<IN, inputSamples, OUT, outputSamples> {
  public:
    LogMel(FIFOBase<IN> &i, FIFOBase<OUT> &o, const log_mel_config_t &c)
        : GenericNode<IN, inputSamples, OUT, outputSamples>(i, o), c_(c) {
        static_assert(std::is_same_v<IN, float> && std::is_same_v<OUT, float>);
    }
    cg_status init() final {
        if (c_.frame_samples != inputSamples || c_.bands != outputSamples ||
            log_mel_init(&c_, &fft_))
            return CG_INIT_FAILURE;
        scratch_.reset(new (std::nothrow) float[2 * c_.fft_length]);
        return scratch_ ? CG_SUCCESS : CG_MEMORY_ALLOCATION_FAILURE;
    }
    int run() final {
        log_mel_compute(&c_, &fft_, this->getReadBuffer(), scratch_.get(),
                        scratch_.get() + c_.fft_length, this->getWriteBuffer());
        return CG_SUCCESS;
    }

  private:
    const log_mel_config_t &c_;
    arm_rfft_fast_instance_f32 fft_{};
    std::unique_ptr<float[]> scratch_;
};
#endif
