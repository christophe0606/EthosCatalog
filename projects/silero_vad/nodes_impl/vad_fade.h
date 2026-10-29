// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdint.h>
// Unsigned elapsed time handles a uint32 tick wrap during the one-second fade.
static inline unsigned vad_fade_alpha(uint32_t now,uint32_t detected,uint32_t duration,int active) {
 uint32_t elapsed=now-detected;
 return active && duration && elapsed<duration ? 255U-(uint64_t)elapsed*255U/duration : 0;
}
