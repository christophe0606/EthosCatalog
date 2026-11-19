// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "../../../common/nodes_impl/c/NodeSupport.hpp"
#include "dtln_test_pattern.hpp"
#include <cmath>

template<typename IN, int ni>
class DtlnFakeSink : public GenericSink<IN, ni> {
public:
    DtlnFakeSink(FIFOBase<IN>& input, const DtlnFakeSinkParams& params)
        : GenericSink<IN, ni>(input), params_(params) {
        static_assert(ni == DTLN_AUDIO_HOP_SAMPLES);
    }
    cg_status init() final { return params_.hw_.stats ? CG_SUCCESS : CG_INIT_FAILURE; }
    int run() final {
        const auto* input = this->getReadBuffer();
        auto& stats = *params_.hw_.stats;
        for (unsigned i = 0; i < ni; ++i) {
            uint32_t bits;
            std::memcpy(&bits, input + i, sizeof(bits));
            if ((bits & 0x7f800000U) == 0x7f800000U) {
                ++stats.nonfinite_samples;
                return -510;
            }
            if (warmup_ < DTLN_AUDIO_FRAME_SAMPLES - DTLN_AUDIO_HOP_SAMPLES) {
                ++warmup_;
                continue;
            }
            const uint32_t section = position_ / dtln::TestPattern::sample_rate;
            const uint32_t within = position_ % dtln::TestPattern::sample_rate;
            // Trim transitions by 250 ms, matching the independent host checks.
            if (within >= 4000 && within < 12000) {
                const auto expected = pattern_.at(position_);
                auto& sums = sums_[section];
                const double error = input[i] - expected.clean;
                const double input_error = expected.microphone - expected.clean;
                ++sums.count;
                sums.input += double(expected.microphone) * expected.microphone;
                sums.output += double(input[i]) * input[i];
                sums.clean += double(expected.clean) * expected.clean;
                sums.residual += error * error;
                sums.input_error += input_error * input_error;
            }
            if (++position_ == dtln::TestPattern::cycle_samples) {
                publish();
                position_ = 0;
            }
        }
        ++stats.output_blocks;
        stats.output_samples += ni;
        const uint32_t elapsed = osKernelGetSysTimerCount() - stats.processing_started;
        stats.processing_ticks_max = std::max(uint32_t(stats.processing_ticks_max), elapsed);
        if (elapsed > uint64_t(osKernelGetSysTimerFreq()) * DTLN_AUDIO_HOP_SAMPLES /
            DTLN_AUDIO_SAMPLE_RATE) ++stats.deadline_misses;
        return CG_SUCCESS;
    }
private:
    struct Sums {
        uint32_t count = 0;
        double input = 0, output = 0, clean = 0, residual = 0, input_error = 0;
    };
    DtlnFakeSinkParams params_;
    dtln::TestPattern pattern_;
    Sums sums_[5];
    uint32_t warmup_ = 0, position_ = 0;

    void publish() {
        DtlnCaseMetrics values[5]{};
        for (unsigned i = 0; i < 5; ++i) {
            const auto& sums = sums_[i];
            values[i] = {sums.count,
                float(std::sqrt(sums.input / sums.count)),
                float(std::sqrt(sums.output / sums.count)),
                float(std::sqrt(sums.residual / sums.count)),
                float(std::sqrt(sums.clean / sums.count))};
        }
        const bool passed = values[0].output_rms < 1e-5f && values[4].output_rms < 1e-5f &&
            values[1].output_rms < 0.95f * values[1].input_rms &&
            values[2].residual_rms < 0.5f * float(std::sqrt(sums_[2].input_error / sums_[2].count)) &&
            values[3].output_rms > 0.4f * values[3].clean_rms &&
            values[3].output_rms < 1.6f * values[3].clean_rms;
        // One core publishes and snapshots under a scheduler lock. No inference
        // or serial work runs inside this short copy.
        const int32_t previous = osKernelLock();
        auto& stats = *params_.hw_.stats;
        std::memcpy(stats.cases, values, sizeof(values));
        ++stats.cycles_completed;
        ++stats.cycle_checks;
        if (!passed) ++stats.cycle_failures;
        ++stats.stats_sequence;
        osKernelRestoreLock(previous);
        std::fill(std::begin(sums_), std::end(sums_), Sums{});
    }
};
