// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "../../../common/nodes_impl/c/NodeSupport.hpp"

template<typename IN, int n>
class SpeakerOutput : public GenericSink<IN, n> {
public:
    SpeakerOutput(FIFOBase<IN>& input, const SpeakerOutputParams& params)
        : GenericSink<IN, n>(input), play_(params.hw_.play_audio) {
        static_assert(n == DTLN_AUDIO_HOP_SAMPLES);
    }

    cg_status init() final { return play_ ? CG_SUCCESS : CG_INIT_FAILURE; }

    int run() final {
        play_(this->getReadBuffer());
        return CG_SUCCESS;
    }

private:
    void (*play_)(const float*);
};
