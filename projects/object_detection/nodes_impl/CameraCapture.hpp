// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "../../../common/nodes_impl/c/NodeSupport.hpp"
#include "board_nodes.h"
template<typename OUT,int no>
class CameraCapture : public GenericSource<OUT,no> {
public:
    CameraCapture(FIFOBase<OUT>& out,const CameraCaptureParams& p)
        : GenericSource<OUT,no>(out),hw_(p.hw_) { static_assert(no==1); }
    int run() final {
        auto& stats=*hw_.stats;
        // Previous frame has passed the display release fence before firing.
        if(stats.captured!=stats.frames_presented) { ++stats.ownership_errors; return -820; }
        uint32_t started=osKernelGetSysTimerCount(); stats.frame_started=started;
        int error=camera_capture(hw_.camera_buffer,2*osKernelGetTickFreq());
        stats.capture_timeouts=camera_timeouts; stats.camera_errors=camera_errors;
        if(error) return error;
        *this->getWriteBuffer()={hw_.camera_buffer,BOARD_CAMERA_WIDTH,BOARD_CAMERA_HEIGHT,
            BOARD_CAMERA_WIDTH,++stats.captured,started};
        return CG_SUCCESS;
    }
private:
    HardwareParams hw_;
};
