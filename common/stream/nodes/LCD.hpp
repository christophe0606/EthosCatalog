// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "NodeSupport.hpp"
class LCD : public StreamNode {
public:
    enum selector { selSpectrum = 0 };
    static std::array<uint16_t,1> selectors;
    explicit LCD(const LCDParams& params) : params_(params) {}
    cg_status init() final {
        const auto& hw = params_.hw_;
        return hw.stats && hw.lcd_magnitude && hw.lcd_peak_hz && hw.framebuffers[0] && hw.framebuffers[1]
            && hw.lcd_touch && hw.ui_touch && hw.ui_release && hw.ui_render && hw.lcd_present ? CG_SUCCESS : CG_INIT_FAILURE;
    }
    cg_status processEvent(int dstPort, Event&& evt) final {
        if (dstPort != 0 || evt.event_id != selectors[selSpectrum] || !evt.wellFormed<TensorPtr<float>>()) return CG_BUFFER_ERROR;
        const auto& hw = params_.hw_;
        uint32_t start = osKernelGetSysTimerCount();
        auto data = evt.get<TensorPtr<float>>();
        bool lock_error = false;
        bool valid = data.lock_shared(lock_error, [&](const arm_cmsis_stream::Tensor<float>& tensor) {
            if (tensor.size() != size_t(params_.fft_size/2+1) || !tensor.buffer()) return false;
            std::memcpy(hw.lcd_magnitude, tensor.buffer(), tensor.size()*sizeof(float));
            return true;
        });
        if (lock_error || !valid) return CG_BUFFER_ERROR;
        // Release the event's payload before rendering or waiting for VSYNC.
        data.reset();
        ++hw.stats->lcd_events_received;
        unsigned peak = 1;
        for (unsigned i = 2; i < unsigned(params_.fft_size/2+1); ++i)
            if (hw.lcd_magnitude[i] > hw.lcd_magnitude[peak]) peak = i;
        *hw.lcd_peak_hz = peak * params_.sample_rate / params_.fft_size;
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
        hw.ui_render(hw.framebuffers[back_],logarithmic_);
        error = hw.lcd_present(hw.framebuffers[back_]);
        if (error) return static_cast<cg_status>(error);
        ++hw.stats->frames_presented; back_ ^= 1U;
        if (osKernelGetSysTimerCount() - start > osKernelGetSysTimerFreq()/params_.fps) ++hw.stats->lcd_deadline_misses;
        return CG_SUCCESS;
    }
private:
    LCDParams params_;
    unsigned back_ = 1;
    bool held_ = false, logarithmic_ = true;
    uint32_t last_contact_ = 0;
};
