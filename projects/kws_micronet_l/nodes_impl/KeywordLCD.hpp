// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "../../../common/nodes_impl/c/NodeSupport.hpp"
#include "keyword_ui.h"
// Called only by the Stream event thread; owns all display/fade state.
class KeywordLCD : public StreamNode {
public:
    enum selector {selKeyword=0,selTick=1};
    static std::array<uint16_t,2> selectors;
    explicit KeywordLCD(const KeywordLCDParams& p):p_(p) {}
    cg_status init() final {
        return p_.hw_.stats && p_.hw_.framebuffers[0] && p_.hw_.framebuffers[1] &&
            p_.hw_.lcd_present && p_.fade_ms>0 && p_.fps>0 ? CG_SUCCESS : CG_INIT_FAILURE;
    }
    cg_status processEvent(int port,Event&& event) final {
        if(port!=0) return CG_BUFFER_ERROR;
        auto& s=*p_.hw_.stats;
        ++s.lcd_events_received;
        if(event.event_id==selectors[selKeyword] && event.wellFormed<uint32_t,uint32_t>()) {
            event.apply<uint32_t,uint32_t>([this](uint32_t word,uint32_t tick) {
                word_=word; detected_=tick;
            });
            return CG_SUCCESS;
        }
        if(event.event_id!=selectors[selTick] || !event.wellFormed<uint32_t>()) return CG_BUFFER_ERROR;
        uint32_t start=osKernelGetSysTimerCount();
        uint32_t elapsed=osKernelGetTickCount()-detected_;
        uint32_t duration=uint64_t(osKernelGetTickFreq())*p_.fade_ms/1000;
        unsigned alpha=word_<10 && elapsed<duration ? 255U-uint64_t(elapsed)*255U/duration : 0;
        keyword_render(p_.hw_.framebuffers[back_],word_,alpha);
        int error=p_.hw_.lcd_present(p_.hw_.framebuffers[back_]);
        if(error) return static_cast<cg_status>(error);
        s.displayed_class=word_; s.display_alpha=alpha;
        ++s.frames_presented; back_^=1U;
        if(osKernelGetSysTimerCount()-start>osKernelGetSysTimerFreq()/p_.fps) ++s.lcd_deadline_misses;
        return CG_SUCCESS;
    }
private:
    KeywordLCDParams p_;
    uint32_t word_=12,detected_=0;
    unsigned back_=1;
};
