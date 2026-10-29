// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "AudioWindow.hpp"
#include <string.h>
int yamnet_window_push(yamnet_window_t *w, const float *input, float *output) {
    memmove(w->samples, w->samples + 160, 320 * sizeof(float));
    memcpy(w->samples + 320, input, 160 * sizeof(float));
    memcpy(output, w->samples, 400 * sizeof(float));
    return ++w->blocks >= 3;
}
