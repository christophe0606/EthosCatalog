// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "LogMel.hpp"
#include <math.h>
#include <string.h>
int log_mel_init(const log_mel_config_t *c, arm_rfft_fast_instance_f32 *fft) {
    if (!c || !fft || !c->frame_samples || c->frame_samples > c->fft_length || !c->bands ||
        !c->window || !c->weights || !(c->offset > 0))
        return -1;
    return arm_rfft_fast_init_f32(fft, c->fft_length) != ARM_MATH_SUCCESS;
}
void log_mel_compute(const log_mel_config_t *c, const arm_rfft_fast_instance_f32 *fft,
                     const float *input, float *frame, float *spectrum, float *output) {
    arm_mult_f32(input, c->window, frame, c->frame_samples);
    memset(frame + c->frame_samples, 0, (c->fft_length - c->frame_samples) * sizeof(float));
    arm_rfft_fast_f32(fft, frame, spectrum, 0);
    float nyquist = fabsf(spectrum[1]);
    spectrum[1] = 0;
    arm_cmplx_mag_f32(spectrum, frame, c->fft_length / 2);
    frame[c->fft_length / 2] = nyquist;
    for (unsigned b = 0; b < c->bands; ++b) {
        float v;
        arm_dot_prod_f32(frame, c->weights + b * (c->fft_length / 2 + 1), c->fft_length / 2 + 1,
                         &v);
        output[b] = logf(v + c->offset);
    }
}
