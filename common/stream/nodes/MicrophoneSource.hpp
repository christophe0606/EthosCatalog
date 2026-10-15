// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "NodeSupport.hpp"
template<typename OUT, int outputSamples>
class MicrophoneSource : public GenericSource<OUT, outputSamples> {
public:
    MicrophoneSource(FIFOBase<OUT>& out, const MicrophoneSourceParams& params)
        : GenericSource<OUT, outputSamples>(out), hw_(params.hw_) { static_assert(outputSamples == 1); }
    cg_status init() final { return hw_.microphone_start && hw_.microphone_read && hw_.stats ? CG_SUCCESS : CG_INIT_FAILURE; }
    int run() final {
        if (!started_) {
            int error = hw_.microphone_start();
            if (error) return error;
            started_ = true;
        }
        OUT* out = this->getWriteBuffer();
        bool gap = false;
        int error = hw_.microphone_read(out->samples, &gap);
        if (error) return error;
        out->info = {osKernelGetSysTimerCount(), gap ? 1U : 0U, 1U, 0.0f};
        if (gap) ++hw_.stats->discontinuities;
        return CG_SUCCESS;
    }
private:
    HardwareParams hw_;
    bool started_ = false;
};
