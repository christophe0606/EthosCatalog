// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "conv3x3.h"
#include <cmath>
#include <initializer_list>
namespace yamnet {
// Runs production MVE on Arm, scalar fallback on a host. Independent scalar
// evaluation covers both layouts, stride combinations and vector tails.
inline bool check_conv3x3() {
    float input[2048], weights[64], expected[512], actual[512];
    for (int i = 0; i < 2048; ++i) input[i] = float((i * 17 % 127) - 63) / 31;
    for (int i = 0; i < 64; ++i) weights[i] = float((i * 13 % 37) - 18) / 29;
    for (int channels : {1, 3, 4, 7})
    for (int nhwc = 0; nhwc < 2; ++nhwc)
    for (int sy = 1; sy <= 2; ++sy)
    for (int sx = 1; sx <= 2; ++sx)
    for (int h = 1; h <= 3; ++h)
    for (int w = 1; w <= 9; ++w) {
        int ih = (h - 1) * sy + 3, iw = (w - 1) * sx + 3;
        for (int i = 0; i < 512; ++i) actual[i] = expected[i] = -12345;
        for (int c = 0; c < channels; ++c) {
            ConvPlane p{input + (nhwc ? c : c * ih * iw),
                expected + (nhwc ? c : c * h * w),
                weights + (nhwc ? c : c * 9),
                nhwc ? iw * channels : iw, nhwc ? channels : 1,
                nhwc ? w * channels : w, nhwc ? channels : 1,
                nhwc ? 3 * channels : 3, nhwc ? channels : 1,
                h, w, sy, sx, float(c - 3) / 7};
            conv3x3_scalar(p);
            p.output = actual + (nhwc ? c : c * h * w);
            conv3x3(p);
        }
        for (int i = 0; i < 512; ++i)
            if (!std::isfinite(actual[i]) || std::fabs(actual[i] - expected[i]) >
                2e-5f * (1 + std::fabs(expected[i]))) return false;
    }
    return true;
}
}
