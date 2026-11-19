// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "../../../common/nodes_impl/c/NodeSupport.hpp"
#include "dtln_test_pattern.hpp"

template<typename MIC, int nm, typename REF, int nr>
class DtlnFakeSource : public NodeBase {
public:
    DtlnFakeSource(FIFOBase<MIC>& mic, FIFOBase<REF>& reference,
                   const DtlnFakeSourceParams& params)
        : mic_(mic), reference_(reference), params_(params) {
        static_assert(nm == DTLN_AUDIO_HOP_SAMPLES && nr == nm);
        static_assert(DTLN_AUDIO_SAMPLE_RATE == dtln::TestPattern::sample_rate);
        static_assert(dtln::TestPattern::cycle_samples % nm == 0);
    }
    cg_status init() final {
        return params_.hw_.stats && osKernelGetTickFreq() % hop_rate == 0 ?
            CG_SUCCESS : CG_INIT_FAILURE;
    }
    int run() final {
        uint32_t now = osKernelGetTickCount();
        const uint32_t period = osKernelGetTickFreq() / hop_rate;
        if (!started_) {
            due_ = now;
            started_ = true;
        }
        if (static_cast<int32_t>(due_ - now) > 0) {
            const osStatus_t status = osDelayUntil(due_);
            now = osKernelGetTickCount();
            // The deadline can expire between the read and RTX's delay check.
            if (status != osOK &&
                (status != osErrorParameter || static_cast<int32_t>(now - due_) < 0))
                return CG_OS_ERROR;
        }
        if (static_cast<int32_t>(now - due_) > 0) {
            ++params_.hw_.stats->source_late_frames;
            // A slow inline inference must give the lower-priority reporting
            // thread CPU time. Yield alone only schedules equal priorities.
            if (osDelay(1) != osOK) return CG_OS_ERROR;
            due_ = osKernelGetTickCount();
        }
        due_ += period;
        auto& stats = *params_.hw_.stats;
        stats.processing_started = osKernelGetSysTimerCount();
        // Each synthetic episode starts after the previous episode's silence.
        if (sample_ == 0) ++stats.input_epoch;
        auto* mic = mic_.getWriteBuffer(nm);
        auto* reference = reference_.getWriteBuffer(nr);
        for (unsigned i = 0; i < nm; ++i) {
            const auto value = pattern_.at(sample_);
            mic[i] = value.microphone;
            reference[i] = value.reference;
            sample_ = (sample_ + 1) % dtln::TestPattern::cycle_samples;
        }
        ++stats.audio_blocks;
        return CG_SUCCESS;
    }
private:
    FIFOBase<MIC>& mic_;
    FIFOBase<REF>& reference_;
    DtlnFakeSourceParams params_;
    dtln::TestPattern pattern_;
    uint32_t sample_ = 0, due_ = 0;
    bool started_ = false;
    static constexpr uint32_t hop_rate = DTLN_AUDIO_SAMPLE_RATE / DTLN_AUDIO_HOP_SAMPLES;
};
