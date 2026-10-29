// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "frontend.h"
#define LOG_MEL_BACKEND_ONLY
#include "../../common/nodes_impl/c/LogMel.hpp"
#include "model/frontend_stream_test.h"
#include "model/frontend_tables.h"
#include "model/frontend_test_data.h"
#define YAMNET_WINDOW_BACKEND_ONLY
#include "nodes_impl/AudioWindow.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
extern "C" {
const log_mel_config_t yamnet_log_mel_config = {400,   512, 64, yamnet_hann, yamnet_mel_weights,
                                                0.001f};
float yamnet_frontend_max_error;
volatile unsigned yamnet_frontend_passed;
int yamnet_frontend_selftest() {
    arm_rfft_fast_instance_f32 fft;
    float in[400], a[512], b[512], out[64];
    if (log_mel_init(&yamnet_log_mel_config, &fft))
        return -450;
    for (unsigned i = 0; i < 400; ++i)
        in[i] = yamnet_frontend_test_pcm[i] / 32768.0f;
    log_mel_compute(&yamnet_log_mel_config, &fft, in, a, b, out);
    for (unsigned i = 0; i < 64; ++i) {
        uint32_t bits;
        std::memcpy(&bits, out + i, 4);
        if ((bits & 0x7f800000U) == 0x7f800000U)
            return -450;
        float e = std::fabs(out[i] - yamnet_frontend_test_expected[i]);
        if (e > yamnet_frontend_max_error)
            yamnet_frontend_max_error = e;
    }
    yamnet_window_t window{};
    float block[160];
    unsigned frame = 0;
    for (unsigned n = 0; n < 5; ++n) {
        for (unsigned i = 0; i < 160; ++i)
            block[i] = stream_pcm[n * 160 + i] / 32768.0f;
        if (!yamnet_window_push(&window, block, in))
            continue;
        for (unsigned i = 0; i < 400; ++i)
            if (in[i] != stream_pcm[frame * 160 + i] / 32768.0f)
                return -450;
        log_mel_compute(&yamnet_log_mel_config, &fft, in, a, b, out);
        for (unsigned i = 0; i < 64; ++i) {
            uint32_t bits;
            std::memcpy(&bits, out + i, 4);
            if ((bits & 0x7f800000U) == 0x7f800000U)
                return -450;
            float e = std::fabs(out[i] - stream_expected[frame][i]);
            if (e > yamnet_frontend_max_error)
                yamnet_frontend_max_error = e;
        }
        ++frame;
    }
    if (frame != 3)
        return -450;
    if (yamnet_frontend_max_error > 0.001f)
        return -450;
    yamnet_frontend_passed = 1;
    printf("YAMNet frontend: PASS max log-mel error %.9f\n", yamnet_frontend_max_error);
    return 0;
}
}
