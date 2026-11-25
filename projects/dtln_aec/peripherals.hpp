// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "../../board/DevKit-E8/audio_output.h"
#include "../../board/DevKit-E8/board_nodes.h"

struct DtlnLcd {
    static constexpr unsigned width = 800, height = 480;
    static_assert(width * height == LCD_PIXELS);

    uint16_t* frames[2];
    unsigned front = 0;
    int status = 0;

    int present(unsigned index) {
        status = lcd_present(frames[index]);
        if (!status) front = index;
        return status;
    }
};

struct DtlnSpeaker {
    static constexpr unsigned sampleRate = DEVKIT_E8_AUDIO_RATE;
    static constexpr unsigned blockFrames = DEVKIT_E8_AUDIO_FRAMES;

    int initStatus = 0;

    int write(const int16_t* frames) const { return devkit_e8_audio_write(frames, blockFrames); }
    int drain(unsigned timeoutMs) const { return devkit_e8_audio_drain(timeoutMs); }
    int setMuted(bool muted) const { return devkit_e8_audio_set_muted(muted); }
};
