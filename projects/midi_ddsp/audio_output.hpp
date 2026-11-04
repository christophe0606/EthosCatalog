// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdint.h>
extern "C" int midi_ddsp_audio_init();
extern "C" int midi_ddsp_audio_write(const int16_t *mono,unsigned frames);
extern "C" int midi_ddsp_audio_drain(unsigned timeout_ms);
extern "C" void midi_ddsp_audio_stop();
