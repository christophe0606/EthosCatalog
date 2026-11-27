// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "../../../common/nodes_impl/c/NodeSupport.hpp"
#include "EventQueue.hpp"
#include "waveform.hpp"

template<typename IN, int samples>
class WaveformCapture : public GenericSink<IN, samples> {
    using History = encodec::waveform::History;
    static_assert(std::is_same_v<IN, float> && std::is_trivially_copyable_v<History>);

public:
    enum Selector { waveform };
    static std::array<uint16_t, 1> selectors;

    WaveformCapture(FIFOBase<IN>& input, EventQueue* queue)
        : GenericSink<IN, samples>(input), events_(queue) {}

    cg_status init() final {
        history_ = {};
        snapshot_ = wrap(snapshotStorage_);
        return snapshot_ ? CG_SUCCESS : CG_MEMORY_ALLOCATION_FAILURE;
    }

    void subscribe(int port, StreamNode& destination, int destPort) final {
        if (port == 0) events_.subscribe(destination, destPort);
    }

    int run() final {
        encodec::waveform::append(history_, this->getReadBuffer(), samples);
        if (snapshot_.use_count() != 1) {
            ++encodecDemoStats.waveform_drops;
            return CG_SUCCESS;
        }

        bool lockError = false;
        const bool copied = snapshot_.lock(lockError, [&](bool shared, RawBuffer& buffer) {
            if (shared) return false;
            std::memcpy(buffer.buffer(), &history_, sizeof(history_));
            return true;
        });
        if (lockError || !copied || !events_.sendAsync(kLowPriority, selectors[waveform], BufferPtr{snapshot_})) {
            ++encodecDemoStats.waveform_drops;
        }
        return CG_SUCCESS;
    }

    // There are no more writers after EOF, so the final event can use the history directly.
    bool finish() {
        auto final = wrap(history_);
        return final && events_.sendAsync(kLowPriority, selectors[waveform], std::move(final));
    }

private:
    static BufferPtr wrap(History& storage) {
        return BufferPtr::create_with(uint32_t{sizeof(storage)}, UniquePtr<std::byte>(&storage, [](void*) {}));
    }

    // A single graph exists at a time. Its dataflow and event workers are joined before reuse.
    inline static History history_{}, snapshotStorage_{};
    EventOutput events_;
    BufferPtr snapshot_;
};

// The generated scheduler owns the selector definition.
template<>
std::array<uint16_t, 1> WaveformCapture<float, ENCODEC_BLOCK_SAMPLES>::selectors;
