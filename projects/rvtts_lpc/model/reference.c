// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
// Host test adapter. Compile against the unmodified, licensed upstream sources.
#include "upstream/prosody_model.h"
#include "upstream/acoustic_stage2.h"
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT
#endif
static ProsodyModel prosody;
static AcousticStage2 acoustic;
EXPORT int rv_init(const char *prosody_path, const char *acoustic_path) {
    return prosody_load(&prosody, prosody_path) || acoustic_stage2_load(&acoustic, acoustic_path);
}
EXPORT int rv_reference(const unsigned char *phones, const unsigned char *stress,
                        const unsigned short *words, unsigned frames,
                        float *conditioning, float *features) {
    float energy[4096], pitch[4096], voicing[4096];
    if (frames > 4096 || prosody_generate(&prosody, phones, stress, words, frames, energy, pitch, voicing)) return -1;
    memset(features, 0, frames * 20 * sizeof(float));
    for (unsigned t=0; t<frames; ++t) {
        conditioning[t*3]=energy[t]; conditioning[t*3+1]=pitch[t]; conditioning[t*3+2]=voicing[t];
        features[t*20]=energy[t]; features[t*20+18]=pitch[t]; features[t*20+19]=voicing[t];
    }
    return acoustic_stage2_generate(&acoustic, phones, stress, energy, pitch, voicing, frames, features);
}
