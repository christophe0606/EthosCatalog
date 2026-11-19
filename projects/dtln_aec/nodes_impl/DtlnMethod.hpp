// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "../../../common/nodes_impl/c/NodeSupport.hpp"
#include <cstring>

template<typename IN, int ni, typename REF, int nr, typename OUT, int no>
class DtlnMethod : public GenericNode21<IN, ni, REF, nr, OUT, no> {
public:
    DtlnMethod(FIFOBase<IN>& input, FIFOBase<REF>& reference, FIFOBase<OUT>& output,
               const DtlnMethodParams& params)
        : GenericNode21<IN, ni, REF, nr, OUT, no>(input, reference, output), params_(params) {
        static_assert(ni == nr && ni == no && (ni == 257 || ni == 512));
    }
    cg_status init() final {
        return params_.hw_.stats && params_.hw_.forward &&
            ((params_.stage == 1 && ni == 257) || (params_.stage == 2 && ni == 512)) ?
            CG_SUCCESS : CG_INIT_FAILURE;
    }
    int run() final {
        const auto* input = this->getReadBuffer1();
        const auto* reference = this->getReadBuffer2();
        auto* output = this->getWriteBuffer();
        auto& stats = *params_.hw_.stats;
        if (input_epoch_ != stats.input_epoch) {
            std::fill(std::begin(state_), std::end(state_), 0.0f);
            input_epoch_ = stats.input_epoch;
        }
        DtlnForwardTiming timing{};
        const int error = params_.hw_.forward(params_.stage, input, state_, reference,
                                               output, next_state_, &timing);
        if (error) return error;
        if (!finite(output, no) || !finite(next_state_, DTLN_STATE_VALUES)) return -511;
        // Copy before returning so every next invocation receives its own state.
        std::memcpy(state_, next_state_, sizeof(state_));
        if (params_.stage == 1) {
            ++stats.stage1_calls;
            stats.stage1_ticks_max = std::max(uint32_t(stats.stage1_ticks_max), timing.adapter_ticks);
        } else {
            ++stats.stage2_calls;
            stats.stage2_ticks_max = std::max(uint32_t(stats.stage2_ticks_max), timing.adapter_ticks);
        }
        return CG_SUCCESS;
    }
private:
    DtlnMethodParams params_;
    uint32_t input_epoch_ = 0;
    float state_[DTLN_STATE_VALUES]{}, next_state_[DTLN_STATE_VALUES]{};
    static bool finite(const float* values, unsigned count) {
        for (unsigned i = 0; i < count; ++i) {
            uint32_t bits;
            std::memcpy(&bits, values + i, sizeof(bits));
            if ((bits & 0x7f800000U) == 0x7f800000U) return false;
        }
        return true;
    }
};
