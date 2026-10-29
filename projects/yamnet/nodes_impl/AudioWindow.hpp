// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#ifndef YAMNET_AUDIO_WINDOW_BACKEND_H
#define YAMNET_AUDIO_WINDOW_BACKEND_H
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    float samples[480];
    unsigned blocks;
} yamnet_window_t;
int yamnet_window_push(yamnet_window_t *, const float *, float *);
#ifdef __cplusplus
}
#endif
#endif
#if defined(__cplusplus) && !defined(YAMNET_WINDOW_BACKEND_ONLY)
#include "../../../common/nodes_impl/c/NodeSupport.hpp"
template <typename IN, int inputSamples, typename OUT, int outputSamples>
class AudioWindow : public GenericNode<IN, inputSamples, OUT, outputSamples> {
  public:
    static std::array<uint16_t, 3> selectors;
    AudioWindow(FIFOBase<IN> &i, FIFOBase<OUT> &o)
        : GenericNode<IN, inputSamples, OUT, outputSamples>(i, o) {
        static_assert(inputSamples == 160 && outputSamples == 400);
    }
    int run() final {
        yamnet_window_push(&window_, this->getReadBuffer(), this->getWriteBuffer());
        return CG_SUCCESS;
    }

  private:
    yamnet_window_t window_{};
};
#endif
