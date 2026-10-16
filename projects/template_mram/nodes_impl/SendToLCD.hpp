// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "../../../common/nodes_impl/c/NodeSupport.hpp"
#include "EventQueue.hpp"
// Application spectrum publication cadence and pipeline diagnostics.
template<typename IN, int inputSamples>
class SendToLCD : public GenericSink<IN,inputSamples> {
public:
    enum selector { selSpectrum = 0 };
    static std::array<uint16_t,1> selectors;
    SendToLCD(FIFOBase<IN>& in, EventQueue* queue, const SendToLCDParams& params)
        : GenericSink<IN,inputSamples>(in), events_(queue), params_(params) { static_assert(std::is_same_v<IN,float> && inputSamples > 0); }
    cg_status init() final {
        if (!params_.hw_.stats || params_.fps <= 0 || params_.sample_rate <= 0) return CG_INIT_FAILURE;
        for (unsigned i = 0; i < slots_.size(); ++i) {
            // Persistent payloads; no sample-buffer allocation in run().
            slots_[i] = TensorPtr<float>::create_with(inputSamples, UniquePtr<float>(payload_[i], [](void*){}));
            if (!slots_[i]) return CG_MEMORY_ALLOCATION_FAILURE;
        }
        return CG_SUCCESS;
    }
    void subscribe(int outputPort, StreamNode& dst, int dstPort) final {
        if (outputPort == 0) events_.subscribe(dst, dstPort);
    }
    int run() final {
        auto* in = this->getReadBuffer();
        auto& stats = *params_.hw_.stats;
        ++stats.spectrum_frames;
        uint32_t now = osKernelGetTickCount();
        if (!sent_ || static_cast<int32_t>(now - next_due_) >= 0) {
            bool posted = false;
            for (unsigned i = 0; i < slots_.size(); ++i) {
                // A queued/executing event owns another reference. That
                // slot stays immutable until the event releases it.
                if (slots_[i].use_count() != 1) continue;
                bool lock_error = false;
                bool copied = slots_[i].lock(lock_error, [&](bool shared, arm_cmsis_stream::Tensor<float>& tensor) {
                    if (shared) return false;
                    std::memcpy(tensor.buffer(), in, inputSamples*sizeof(IN));
                    return true;
                });
                if (lock_error) return CG_OS_ERROR;
                if (!copied) continue;
                posted = events_.sendAsync(kNormalPriority, selectors[selSpectrum], slots_[i]);
                break;
            }
            if (posted) {
                ++stats.lcd_events_sent;
                const uint32_t period = osKernelGetTickFreq()/params_.fps;
                next_due_ = sent_ ? next_due_ + period : now + period;
                if (static_cast<int32_t>(now - next_due_) >= 0) next_due_ = now + period;
                sent_ = true;
            }
            else ++stats.lcd_events_dropped;
        }
        uint32_t elapsed = osKernelGetSysTimerCount() - stats.audio_started;
        if (elapsed > stats.audio_ticks_max) stats.audio_ticks_max = elapsed;
        if (elapsed > uint64_t(osKernelGetSysTimerFreq())*params_.block_samples/params_.sample_rate)
            ++stats.audio_deadline_misses;
        return CG_SUCCESS;
    }
private:
    EventOutput events_;
    SendToLCDParams params_;
    std::array<TensorPtr<float>,3> slots_;
    float payload_[3][inputSamples];
    uint32_t next_due_ = 0;
    bool sent_ = false;
};
