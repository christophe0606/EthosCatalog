// SPDX-License-Identifier: Apache-2.0
#include "MFCC.hpp"
#include <math.h>
#include <string.h>
int mfcc_init(const mfcc_config_t *c, arm_rfft_fast_instance_f32 *fft)
{
    if (!c || !fft || !c->frame_samples || c->frame_samples > c->fft_length ||
        !c->mel_filters || !c->features || !c->window || !c->filter_coefficients ||
        !c->filter_positions || !c->filter_lengths || !c->dct || !(c->log_floor > 0)) return -1;
    for (uint32_t i=0; i<c->mel_filters; ++i)
        if (!c->filter_lengths[i] || c->filter_positions[i]+c->filter_lengths[i] > c->fft_length/2+1) return -1;
    return arm_rfft_fast_init_f32(fft, c->fft_length) != ARM_MATH_SUCCESS;
}
void mfcc_compute(const mfcc_config_t *c, const arm_rfft_fast_instance_f32 *fft,
                  const float *input, float *frame, float *spectrum, float *mel, float *output)
{
    arm_mult_f32(input,c->window,frame,c->frame_samples);
    memset(frame+c->frame_samples,0,(c->fft_length-c->frame_samples)*sizeof(float));
    arm_rfft_fast_f32(fft,frame,spectrum,0);
    const float nyquist = fabsf(spectrum[1]);
    spectrum[1] = 0;
    arm_cmplx_mag_f32(spectrum,frame,c->fft_length/2);
    frame[c->fft_length/2] = nyquist;
    const float *weights = c->filter_coefficients;
    for (uint32_t i=0; i<c->mel_filters; ++i) {
        float energy;
        arm_dot_prod_f32(frame+c->filter_positions[i],weights,c->filter_lengths[i],&energy);
        // Configurable floor: ML Eval Kit uses FLT_MIN; arm_mfcc_f32 uses 1e-6.
        mel[i] = logf(energy+c->log_floor);
        weights += c->filter_lengths[i];
    }
    for (uint32_t i=0; i<c->features; ++i)
        arm_dot_prod_f32(c->dct+i*c->mel_filters,mel,c->mel_filters,output+i);
}
