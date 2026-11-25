// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "../../../common/nodes_impl/c/NodeSupport.hpp"
#include "EventQueue.hpp"
#include "IdentifiedNode.hpp"
#include "waveform_display.hpp"

template<typename MIC, int nm, typename OUT, int no>
class WaveformCapture : public GenericSink<MIC, nm>, public ResettableStreamState {
public:
    enum Selector { waveform };
    static std::array<uint16_t, 1> selectors;

    WaveformCapture(FIFOBase<MIC>& microphone, FIFOBase<OUT>& output, EventQueue* queue)
        : GenericSink<MIC, nm>(microphone), output_(output), events_(queue) {
        static_assert(nm == DTLN_AUDIO_HOP_SAMPLES && no == nm);
    }

    cg_status init() final {
        snapshot_ = wrap(snapshotStorage_);
        if (!snapshot_) return CG_MEMORY_ALLOCATION_FAILURE;
        return CG_SUCCESS;
    }

    void resetState() final {
        dtln::waveform::reset(history_);
        offerPeriod_ = (osKernelGetTickFreq() + displayFps - 1) / displayFps;
        lastOfferTick_ = osKernelGetTickCount() - offerPeriod_;
    }

    void subscribe(int port, StreamNode& destination, int destPort) final {
        if (port == 0) events_.subscribe(destination, destPort);
    }

    int run() final {
        const auto* microphone = this->getReadBuffer();
        dtln::waveform::append(history_, microphone, output_.getReadBuffer(no));

        const uint32_t now = osKernelGetTickCount();
        // A queued or rendering event owns another reference. Skip this offer while it is busy.
        if (now - lastOfferTick_ < offerPeriod_ || snapshot_.use_count() != 1) return CG_SUCCESS;

        bool lockError = false;
        const bool copied = snapshot_.lock(lockError, [&](bool shared, RawBuffer& buffer) {
            if (shared) return false;
            std::memcpy(buffer.buffer(), &history_, sizeof(history_));
            return true;
        });
        if (!lockError && copied) {
            lastOfferTick_ = now;
            events_.sendAsync(kLowPriority, selectors[waveform], BufferPtr{snapshot_});
        }
        return CG_SUCCESS;
    }

    bool finish() {
        // The dataflow is finished. Move the final history into an event without copying its bytes.
        auto final = wrap(history_);
        return final && events_.sendAsync(kLowPriority, selectors[waveform], std::move(final));
    }

private:
    static BufferPtr wrap(dtln::waveform::History& storage) {
        // The graph owns the static payload storage. Stream owns the event references and lock.
        return BufferPtr::create_with(uint32_t{sizeof(storage)}, UniquePtr<std::byte>(&storage, [](void*) {}));
    }

    static constexpr unsigned displayFps = 10;
    static_assert(std::is_trivially_copyable_v<dtln::waveform::History>);
    inline static dtln::waveform::History history_{}, snapshotStorage_{};

    FIFOBase<OUT>& output_;
    EventOutput events_;
    BufferPtr snapshot_;
    uint32_t offerPeriod_ = 0;
    uint32_t lastOfferTick_ = 0;
};
