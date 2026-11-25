// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "../../../common/nodes_impl/c/NodeSupport.hpp"
#include "IdentifiedNode.hpp"
#include "peripherals.hpp"
#include "waveform_display.hpp"

class WaveformDisplay : public StreamNode, public ResettableStreamState {
public:
    enum Selector { waveform };
    static std::array<uint16_t, 1> selectors;

    explicit WaveformDisplay(const WaveformDisplayParams& params)
        : lcd_(params.hw_.lcd), stats_(params.hw_.stats) {}

    cg_status init() final {
        return lcd_ && stats_ ? CG_SUCCESS : CG_INIT_FAILURE;
    }

    void resetState() final { stats_->lcd_error = lcd_->status; }

    cg_status processEvent(int port, Event&& event) final {
        if (port != 0 || event.event_id != selectors[waveform] || !event.wellFormed<BufferPtr>()) {
            return CG_BUFFER_ERROR;
        }
        if (stats_->lcd_error) return CG_SUCCESS;

        auto payload = event.get<BufferPtr>();
        if (!payload) return CG_BUFFER_ERROR;
        const unsigned back = lcd_->front ^ 1U;
        bool lockError = false;
        const bool rendered = payload.lock_shared(lockError, [&](const RawBuffer& buffer) {
            const auto* data = buffer.buffer();
            if (!data || buffer.buf_size != sizeof(dtln::waveform::History)) return false;
            const auto& history = *reinterpret_cast<const dtln::waveform::History*>(data);
            dtln::waveform::render(lcd_->frames[back], history);
            return true;
        });
        if (lockError) return CG_OS_ERROR;
        if (!rendered) return CG_BUFFER_ERROR;

        // Let capture reuse the slot while the driver waits for scanout.
        payload.reset();
        stats_->lcd_error = lcd_->present(back);
        if (!stats_->lcd_error) ++stats_->lcd_frames;
        return CG_SUCCESS;
    }

private:
    DtlnLcd* const lcd_;
    StreamStats* const stats_;
};
