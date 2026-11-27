// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "../../../common/nodes_impl/c/NodeSupport.hpp"
#include "board_nodes.h"
#include "waveform.hpp"

class WaveformDisplay : public StreamNode {
public:
    enum Selector { waveform };
    static std::array<uint16_t, 1> selectors;

    explicit WaveformDisplay(const WaveformDisplayParams& params)
        : lcd_(params.hw_.lcd), stats_(params.hw_.stats) {}

    cg_status init() final {
        return lcd_ && stats_ ? CG_SUCCESS : CG_INIT_FAILURE;
    }

    cg_status processEvent(int port, Event&& event) final {
        if (port != 0 || event.event_id != selectors[waveform] || !event.wellFormed<BufferPtr>()) {
            stats_->lcd_error = CG_BUFFER_ERROR;
            return CG_SUCCESS;
        }
        if (lcd_->status) return CG_SUCCESS;

        auto payload = event.get<BufferPtr>();
        if (!payload) {
            stats_->lcd_error = CG_BUFFER_ERROR;
            return CG_SUCCESS;
        }
        const unsigned back = lcd_->front ^ 1U;
        bool lockError = false;
        const bool rendered = payload.lock_shared(lockError, [&](const RawBuffer& buffer) {
            if (!buffer.buffer() || buffer.buf_size != sizeof(encodec::waveform::History)) return false;
            const auto& history = *reinterpret_cast<const encodec::waveform::History*>(buffer.buffer());
            encodec::waveform::render(lcd_->frames[back], history);
            return true;
        });
        if (lockError || !rendered) {
            const auto error = lockError ? CG_OS_ERROR : CG_BUFFER_ERROR;
            stats_->lcd_error = error;
            return CG_SUCCESS;
        }

        // Release the snapshot before waiting for scanout of the new frame.
        payload.reset();
        lcd_->status = lcd_present(lcd_->frames[back]);
        if (lcd_->status) {
            stats_->lcd_error = lcd_->status;
        } else {
            lcd_->front = back;
            ++stats_->lcd_frames;
        }
        return CG_SUCCESS;
    }

private:
    EnCodecLcd* const lcd_;
    EnCodecDemoStats* const stats_;
};
