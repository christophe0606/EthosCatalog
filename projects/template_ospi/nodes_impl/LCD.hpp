// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "../../../common/nodes_impl/c/NodeSupport.hpp"
#include <new>
// Application spectrum state and touch policy belong to template_ospi.
class LCD : public StreamNode {
public:
    enum selector { selSpectrum = 0 };
    static std::array<uint16_t,1> selectors;
    explicit LCD(const LCDParams& params) : params_(params) {}
    cg_status init() final {
        const auto& hw = params_.hw_;
        if (!hw.stats || !hw.framebuffers[0] || !hw.framebuffers[1] || !hw.lcd_touch
            || !hw.ui_touch || !hw.ui_release || !hw.ui_render || !hw.lcd_present
            || params_.fft_size < 2 || params_.fft_size % 2 || params_.sample_rate <= 0 || params_.fps <= 0)
            return CG_INIT_FAILURE;
        spectrum_magnitude_.reset(new (std::nothrow) float[params_.fft_size/2+1]{});
        if (!spectrum_magnitude_) return CG_MEMORY_ALLOCATION_FAILURE;
        spectrum_peak_hz_ = 0;
        return CG_SUCCESS;
    }
    uint32_t peak_hz() const { return spectrum_peak_hz_; }
    cg_status processEvent(int dstPort, Event&& evt) final {
        if (dstPort != 0 || evt.event_id != selectors[selSpectrum] || !evt.wellFormed<TensorPtr<float>>()) return CG_BUFFER_ERROR;
        const auto& hw = params_.hw_;
        uint32_t start = osKernelGetSysTimerCount();
        auto data = evt.get<TensorPtr<float>>();
        bool lock_error = false;
        bool valid = data.lock_shared(lock_error, [&](const arm_cmsis_stream::Tensor<float>& tensor) {
            if (tensor.size() != size_t(params_.fft_size/2+1) || !tensor.buffer()) return false;
            std::memcpy(spectrum_magnitude_.get(), tensor.buffer(), tensor.size()*sizeof(float));
            return true;
        });
        if (lock_error || !valid) return CG_BUFFER_ERROR;
        // Release the event's payload before rendering or waiting for VSYNC.
        data.reset();
        ++hw.stats->lcd_events_received;
        unsigned peak = 1;
        for (unsigned i = 2; i < unsigned(params_.fft_size/2+1); ++i)
            if (spectrum_magnitude_[i] > spectrum_magnitude_[peak]) peak = i;
        spectrum_peak_hz_ = peak * params_.sample_rate / params_.fft_size;
        unsigned x = 0, y = 0; bool contact = false;
        int error = hw.lcd_touch(&x,&y,&contact);
        if (error) return static_cast<cg_status>(error);
        uint32_t now = osKernelGetTickCount();
        if (contact) {
            if (hw.ui_touch(x,y,!held_)) logarithmic_ = !logarithmic_;
            held_ = true; last_contact_ = now;
        } else if (now - last_contact_ > osKernelGetTickFreq()*150U/1000U) {
            held_ = false; hw.ui_release();
        }
        hw.ui_render(hw.framebuffers[back_],spectrum_magnitude_.get(),logarithmic_);
        error = hw.lcd_present(hw.framebuffers[back_]);
        if (error) return static_cast<cg_status>(error);
        ++hw.stats->frames_presented; back_ ^= 1U;
        if (osKernelGetSysTimerCount() - start > osKernelGetSysTimerFreq()/params_.fps) ++hw.stats->lcd_deadline_misses;
        return CG_SUCCESS;
    }
private:
    LCDParams params_;
    // Retain a node-owned snapshot so queued payloads can be released before VSYNC.
    std::unique_ptr<float[]> spectrum_magnitude_;
    uint32_t spectrum_peak_hz_ = 0;
    unsigned back_ = 1;
    bool held_ = false, logarithmic_ = true;
    uint32_t last_contact_ = 0;
};
