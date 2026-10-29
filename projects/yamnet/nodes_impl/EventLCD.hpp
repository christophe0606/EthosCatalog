// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "../../../common/nodes_impl/c/NodeSupport.hpp"
#include "diagnostics.h"
#include "yamnet_ui.h"
class EventLCD : public StreamNode {
  public:
    static std::array<uint16_t, 3> selectors;
    explicit EventLCD(const EventLCDParams &p) : p_(p) {}
    cg_status init() final {
        return p_.hw_.framebuffers[0] && p_.hw_.lcd_present ? CG_SUCCESS : CG_INIT_FAILURE;
    }
    cg_status processEvent(int port, Event &&e) final {
        if (port != 0 || e.event_id != selectors[2] || !e.wellFormed<uint32_t, uint32_t, float>())
            return CG_BUFFER_ERROR;
        unsigned row = 5, index = 521;
        float score = 0;
        e.apply<uint32_t, uint32_t, float>([&](uint32_t r, uint32_t i, float v) {
            row = r;
            index = i;
            score = v;
        });
        if (row >= 5 || index >= 521)
            return CG_BUFFER_ERROR;
        top_[row] = index;
        scores_[row] = score;
        if (row == 4) {
            yamnet_render(p_.hw_.framebuffers[back_], top_, scores_, p_.hw_.stats);
            int error = p_.hw_.lcd_present(p_.hw_.framebuffers[back_]);
            if (error)
                return static_cast<cg_status>(error);
            back_ ^= 1;
            ++p_.hw_.stats->frames_presented;
            if (p_.hw_.stats->frames_presented == 1 || p_.hw_.stats->frames_presented % 10 == 0)
                yamnet_print_health(p_.hw_.stats);
        }
        return CG_SUCCESS;
    }

  private:
    EventLCDParams p_;
    unsigned top_[5]{};
    float scores_[5]{};
    unsigned back_ = 1;
};
