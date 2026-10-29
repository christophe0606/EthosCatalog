// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "../../../common/nodes_impl/c/NodeSupport.hpp"
#include "EventQueue.hpp"
#include <atomic>
template <typename IN, int inputSamples>
class SendToNetwork : public GenericSink<IN, inputSamples> {
  public:
    static std::array<uint16_t, 3> selectors;
    SendToNetwork(FIFOBase<IN> &i, EventQueue *q, const SendToNetworkParams &p)
        : GenericSink<IN, inputSamples>(i), requests_(q), p_(p) {
        static_assert(inputSamples == 64);
    }
    cg_status init() final { return p_.hw_.stats ? CG_SUCCESS : CG_INIT_FAILURE; }
    void subscribe(int port, StreamNode &dst, int dstPort) final {
        if (port == 0)
            requests_.subscribe(dst, dstPort);
    }
    cg_status processEvent(int port, Event &&e) final {
        if (port != 0 || e.event_id != selectors[1])
            return CG_BUFFER_ERROR;
        busy_.store(false, std::memory_order_release);
        return CG_SUCCESS;
    }
    int run() final {
        auto *in = this->getReadBuffer();
        auto &s = *p_.hw_.stats;
        if (++blocks_ > 2) {
            std::memcpy(ring_ + position_ * 64, in, 64 * sizeof(float));
            position_ = (position_ + 1) % 96;
            ++frames_;
            ++s.feature_frames;
            if (frames_ >= 96 && (frames_ - 96) % 48 == 0) {
                ++s.patches;
                if (pending_)
                    ++s.coalesced_windows;
                for (unsigned f = 0; f < 96; ++f)
                    std::memcpy(latest_ + f * 64, ring_ + ((position_ + f) % 96) * 64,
                                64 * sizeof(float));
                pending_ = true;
            }
            if (pending_ && !busy_.load(std::memory_order_acquire)) {
                std::memcpy(in_flight_, latest_, sizeof(latest_));
                busy_.store(true, std::memory_order_release);
                if (!requests_.sendAsync(kNormalPriority, selectors[0],
                                         uint64_t(reinterpret_cast<uintptr_t>(in_flight_)))) {
                    busy_.store(false);
                    ++s.request_errors;
                    return CG_BUFFER_ERROR;
                }
                pending_ = false;
                ++s.inference_requests;
            }
        }
        if (blocks_ % 100 == 0)
            s.audio_stack_free = osThreadGetStackSpace(osThreadGetId());
        uint32_t ticks = osKernelGetSysTimerCount() - s.audio_started;
        if (ticks > s.audio_ticks_max)
            s.audio_ticks_max = ticks;
        if (ticks > osKernelGetSysTimerFreq() / 100)
            ++s.audio_deadline_misses;
        return CG_SUCCESS;
    }

  private:
    EventOutput requests_;
    SendToNetworkParams p_;
    std::atomic<bool> busy_{false};
    float ring_[6144]{}, latest_[6144]{}, in_flight_[6144]{};
    unsigned position_ = 0, frames_ = 0, blocks_ = 0;
    bool pending_ = false;
};
