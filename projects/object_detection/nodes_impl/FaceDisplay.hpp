// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "stream_types.h"
#ifdef __cplusplus
extern "C" {
#endif
void face_render(uint16_t *lcd,const uint8_t *rgb,const FaceBox *boxes,uint32_t count);
#ifdef __cplusplus
}
#ifndef DETECTION_HOST_TEST
#include "../../../common/nodes_impl/c/NodeSupport.hpp"
#include "board_nodes.h"
template<typename IN,int ni,typename FRAME,int nf>
class FaceDisplay : public NodeBase {
public:
    FaceDisplay(FIFOBase<IN>& in,FIFOBase<FRAME>& frame,const FaceDisplayParams& p)
        : boxes_(in),frame_(frame),hw_(p.hw_),period_ms_(p.period_ms) { static_assert(ni==1 && nf==1); }
    int run() final {
        const auto boxes=*boxes_.getReadBuffer(ni); const auto frame=*frame_.getReadBuffer(nf); auto& stats=*hw_.stats;
        if(frame.sequence!=stats.frames_presented+1 || boxes.sequence!=frame.sequence || boxes.started!=frame.started) {
            ++stats.ownership_errors; return -821;
        }
        ++stats.nms; stats.last_detections=boxes.count; stats.detections+=boxes.count;
        uint16_t *buffer=hw_.framebuffers[next_];
        face_render(buffer,frame.rgb,boxes.boxes,boxes.count);
        uint32_t work=osKernelGetSysTimerCount()-frame.started;
        if(work>stats.processing_ticks_max) stats.processing_ticks_max=work;
        int error=lcd_present(buffer);
        if(error) return error;
        // lcd_present waits for the new address to latch and previous scanout
        // to finish before that previous buffer may be reused.
        next_^=1; ++stats.frames_presented;
        uint32_t elapsed=osKernelGetSysTimerCount()-frame.started;
        uint32_t period=(uint64_t(osKernelGetSysTimerFreq())*period_ms_)/1000;
        stats.last_frame_ticks=elapsed;
        if(elapsed>stats.frame_ticks_max) stats.frame_ticks_max=elapsed;
        if(elapsed>period) ++stats.frame_deadline_misses;
        else {
            uint32_t delay=uint64_t(period-elapsed)*osKernelGetTickFreq()/osKernelGetSysTimerFreq();
            if(delay) osDelay(delay);
        }
        return CG_SUCCESS;
    }
private:
    FIFOBase<IN>& boxes_; FIFOBase<FRAME>& frame_;
    HardwareParams hw_; uint32_t period_ms_; unsigned next_=1;
};
#endif
#endif
