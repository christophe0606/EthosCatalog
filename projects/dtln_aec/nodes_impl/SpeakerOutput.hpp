// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "../../../common/nodes_impl/c/NodeSupport.hpp"
#include "hardware_params.h"
#include "IdentifiedNode.hpp"
#include "peripherals.hpp"

template<typename IN, int n>
class SpeakerOutput : public GenericSink<IN, n>, public ResettableStreamState, public ContextSwitch {
public:
    SpeakerOutput(FIFOBase<IN>& input, const SpeakerOutputParams& params)
        : GenericSink<IN, n>(input), speaker_(params.hw_.speaker), stats_(params.hw_.stats) {
        static_assert(DTLN_AUDIO_SAMPLE_RATE == DtlnSpeaker::sampleRate);
        static_assert(n == DTLN_AUDIO_HOP_SAMPLES && DtlnSpeaker::blockFrames % n == 0);
    }

    void resetState() final {
        pending_ = 0;
        stats_->speaker_error = speaker_->initStatus;
    }

    cg_status init() final {
        return speaker_ && stats_ ? CG_SUCCESS : CG_INIT_FAILURE;
    }

    int pause() final {
        if (!speaker_->initStatus) {
            const int error = speaker_->setMuted(true);
            if (!stats_->speaker_error) stats_->speaker_error = error;
        }
        return CG_SUCCESS;
    }

    int resume() final {
        if (!stats_->speaker_error) stats_->speaker_error = speaker_->setMuted(false);
        return CG_SUCCESS;
    }

    int run() final {
        const auto* samples = this->getReadBuffer();
        if (stats_->speaker_error) return CG_SUCCESS;
        for (unsigned i = 0; i < n; ++i) {
            float sample = samples[i];
            // Inspect the representation so this also works with finite-math optimizations.
            uint32_t bits;
            std::memcpy(&bits, &sample, sizeof(bits));
            if ((bits & 0x7f800000U) == 0x7f800000U) sample = 0.0f;
            sample = std::clamp(sample, -1.0f, 32767.0f / 32768.0f);
            block_[pending_++] = static_cast<int16_t>(sample * 32768.0f);
        }
        if (pending_ == DtlnSpeaker::blockFrames) writeBlock();
        return CG_SUCCESS;
    }

    void finish() {
        if (stats_->speaker_error) return;
        if (pending_) {
            std::fill(block_ + pending_, block_ + DtlnSpeaker::blockFrames, int16_t{0});
            writeBlock();
        }
        // Allow the speaker queue and hardware FIFO to drain at clip end.
        if (!stats_->speaker_error) stats_->speaker_error = speaker_->drain(200);
    }

    void stop() {
        pause();
        // Retire previously queued blocks under mute before starting another graph.
        if (!stats_->speaker_error) stats_->speaker_error = speaker_->drain(200);
    }

private:
    void writeBlock() {
        stats_->speaker_error = speaker_->write(block_);
        pending_ = 0;
    }

    DtlnSpeaker* const speaker_;
    StreamStats* const stats_;
    int16_t block_[DtlnSpeaker::blockFrames];
    unsigned pending_ = 0;
};
