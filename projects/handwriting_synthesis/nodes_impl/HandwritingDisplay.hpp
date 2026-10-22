// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "../../../common/nodes_impl/c/NodeSupport.hpp"
#include "handwriting_protocol.hpp"
#include "handwriting_layout.hpp"
#include "handwriting_ink.hpp"
#include "handwriting_damage.hpp"
#include <cstdio>
class HandwritingDisplay:public StreamNode {
public:
    enum selector {selStroke}; static std::array<uint16_t,1> selectors;
    explicit HandwritingDisplay(const HandwritingDisplayParams& p):hw_(p.hw_) {}
    cg_status init() final {
        std::memset(frames_,0,sizeof(frames_));
        return static_cast<cg_status>(hw_.lcd_init(frames_[0]));
    }
    cg_status processEvent(int port,Event&& evt) final {
        if(port || evt.event_id!=selectors[selStroke] || !evt.wellFormed<uint64_t>()) return CG_BUFFER_ERROR;
        const auto result=*reinterpret_cast<const handwriting::Result*>(uintptr_t(evt.get<uint64_t>()));
        if(!handwriting::current(result.request)) { ++hw_.stats->stale_results; return CG_SUCCESS; }
        if(result.kind==1) {
            auto error=acquire(); if(error) return error;
            generation_=result.request.generation; layout_.reset(); full_=false; overflow_reported_=false;
            std::memset(frames_[back_],0,sizeof(frames_[0])); ++hw_.stats->clears;
            pending_damage_=handwriting::Damage::full();
            hw_.stats->lines=1;
            hw_.stats->sentence_started_ms=osKernelGetTickCount();
            hw_.stats->sentence_elapsed_ms=hw_.stats->sentence_points=0;
            hw_.stats->forward_ticks_sum=hw_.stats->lcd_ticks_sum=0;
            return present(pending_damage_);
        }
        if(result.request.generation!=generation_) return CG_BUFFER_ERROR;
        if(result.kind>1) {
            auto error=acquire(); if(error) return error;
            hw_.stats->sentence_elapsed_ms=osKernelGetTickCount()-hw_.stats->sentence_started_ms;
            printf("Sentence %lu %s; lines=%u points=%lu elapsed_ticks=%lu tick_hz=%lu.\n",(unsigned long)generation_,result.kind==2?"complete":"reached model step limit",layout_.lines(),(unsigned long)hw_.stats->sentence_points,(unsigned long)hw_.stats->sentence_elapsed_ms,(unsigned long)osKernelGetTickFreq());
            return CG_SUCCESS;
        }
        if(full_) return CG_SUCCESS;
        auto segment=layout_.add(result.point.dx,result.point.dy,result.point.pen_up,result.character,result.word_boundary,result.punctuation);
        if(segment.rejected) { report_overflow();return CG_SUCCESS; }
        if(segment.full) {
            full_=true;++hw_.stats->screen_full;
            printf("Sentence %lu exceeds three LCD lines; remaining strokes hidden.\n",(unsigned long)generation_);
            return CG_SUCCESS;
        }
        if(segment.wrapped) { ++hw_.stats->wraps;hw_.stats->lines=layout_.lines(); }
        ++hw_.stats->drawn;
        ++hw_.stats->sentence_points;
        hw_.stats->sentence_elapsed_ms=osKernelGetTickCount()-hw_.stats->sentence_started_ms;
        if(!segment.ink) return CG_SUCCESS;
        auto error=acquire(); if(error) return error;
        auto changed=pending_damage_;
        hw_.stats->lcd_copy_bytes+=pending_damage_.copy(frames_[back_],frames_[back_^1]);
        handwriting::Damage stroke_damage;
        bool clipped=false;
        if(!handwriting::draw_ink(segment.fx0,segment.fy0,segment.fx1,segment.fy1,[&](int x,int y,uint16_t color) {
            if(x>=0 && x<800 && y>=0 && y<480) {
                auto &old=frames_[back_][handwriting::Layout::native_index(x,y)];
                // Union of round pen segments: joining/overlapping segments
                // must never darken an edge or repeatedly brighten it.
                if(color>old) { old=color;stroke_damage.include(x,y); }
            }
            else clipped=true;
        })) clipped=true;
        if(clipped) report_overflow();
        changed.include(stroke_damage);pending_damage_=stroke_damage;
        return present(changed);
    }
private:
    void report_overflow() {
        ++hw_.stats->ink_overflows;
        if(!overflow_reported_) {
            overflow_reported_=true;
            printf("Sentence %lu rejected extreme coordinates or has ink outside the LCD; full text may not fit.\n",(unsigned long)generation_);
        }
    }
    cg_status acquire() {
        uint32_t start=osKernelGetSysTimerCount(); int error=hw_.lcd_acquire();
        if(error) hw_.stats->error=error;
        uint32_t ticks=osKernelGetSysTimerCount()-start;
        hw_.stats->lcd_ticks_sum+=ticks;
        if(ticks>hw_.stats->lcd_wait_ticks_max) hw_.stats->lcd_wait_ticks_max=ticks;
        return static_cast<cg_status>(error);
    }
    cg_status present(const handwriting::Damage& changed) {
        uint32_t start=osKernelGetSysTimerCount();
        int error=hw_.lcd_present(frames_[back_],changed.native_x(),changed.native_y(),changed.native_width(),changed.native_height());
        if(error) { hw_.stats->error=error;return static_cast<cg_status>(error); }
        if(changed.native_width()) {
            unsigned first=(changed.native_x()*2U)&~31U;
            unsigned last=((changed.native_x()+changed.native_width())*2U+31U)&~31U;
            hw_.stats->lcd_clean_bytes+=uint64_t(last-first)*changed.native_height();
        }
        uint32_t ticks=osKernelGetSysTimerCount()-start;
        hw_.stats->lcd_ticks_sum+=ticks;
        if(ticks>hw_.stats->lcd_ticks_max) hw_.stats->lcd_ticks_max=ticks;
        if(ticks>osKernelGetSysTimerFreq()/10) ++hw_.stats->lcd_deadline_misses;
        ++hw_.stats->frames_presented;back_^=1;return CG_SUCCESS;
    }
    static uint16_t frames_[2][480*800];
    HardwareParams hw_; handwriting::Layout layout_;
    handwriting::Damage pending_damage_;
    uint32_t generation_=0; unsigned back_=1; bool full_=false,overflow_reported_=false;
};
