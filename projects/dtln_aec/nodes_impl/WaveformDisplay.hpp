// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "../../../common/nodes_impl/c/NodeSupport.hpp"

template<typename MIC, int nm, typename OUT, int no>
class WaveformDisplay : public GenericSink<MIC, nm> {
public:
    WaveformDisplay(FIFOBase<MIC>& microphone, FIFOBase<OUT>& output,
                    const WaveformDisplayParams& params)
        : GenericSink<MIC, nm>(microphone), output_(output), display_(params.hw_.display_waveform) {
        static_assert(nm == DTLN_AUDIO_HOP_SAMPLES && no == nm);
    }

    cg_status init() final { return display_ ? CG_SUCCESS : CG_INIT_FAILURE; }

    int run() final {
        const auto* microphone = this->getReadBuffer();
        display_(microphone, output_.getReadBuffer(no));
        return CG_SUCCESS;
    }

private:
    FIFOBase<OUT>& output_;
    void (*display_)(const float*, const float*);
};
