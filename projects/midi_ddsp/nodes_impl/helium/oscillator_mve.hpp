// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
// Private oscillator backend; only the owning Synth includes this interface.
extern "C" void midi_ddsp_oscillator_mve(const float *a,const float *b,float hz,
 float next_hz,double *phase,const float *table,const float *window,float *out);
