// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "HandwritingDisplay.hpp"
alignas(32) uint16_t HandwritingDisplay::frames_[2][480*800] __attribute__((section(".bss.lcd_frame_buf")));
