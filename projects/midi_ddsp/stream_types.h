// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdint.h>
// Control pair layout: raw0[126],Hz0,raw1[126],Hz1,flags,reserved.
// Flags are project policy: active dry stream, finite endpoint, new stream.
enum { MIDI_DRY_ACTIVE=1, MIDI_FINITE_ENDPOINT=2, MIDI_RESET_SYNTH=4, MIDI_SYNTH_ENABLED=8 };
#define MIDI_CONTROL_PAIR_FLOATS 256
