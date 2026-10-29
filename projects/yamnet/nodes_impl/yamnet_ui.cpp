// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "yamnet_ui.h"
#include <cctype>
#include <cstdio>
#include <cstring>
static const uint8_t letters[26][5] = {
    {0x7e, 0x11, 0x11, 0x11, 0x7e}, {0x7f, 0x49, 0x49, 0x49, 0x36}, {0x3e, 0x41, 0x41, 0x41, 0x22},
    {0x7f, 0x41, 0x41, 0x22, 0x1c}, {0x7f, 0x49, 0x49, 0x49, 0x41}, {0x7f, 0x09, 0x09, 0x09, 0x01},
    {0x3e, 0x41, 0x49, 0x49, 0x7a}, {0x7f, 0x08, 0x08, 0x08, 0x7f}, {0x00, 0x41, 0x7f, 0x41, 0x00},
    {0x20, 0x40, 0x41, 0x3f, 0x01}, {0x7f, 0x08, 0x14, 0x22, 0x41}, {0x7f, 0x40, 0x40, 0x40, 0x40},
    {0x7f, 0x02, 0x0c, 0x02, 0x7f}, {0x7f, 0x04, 0x08, 0x10, 0x7f}, {0x3e, 0x41, 0x41, 0x41, 0x3e},
    {0x7f, 0x09, 0x09, 0x09, 0x06}, {0x3e, 0x41, 0x51, 0x21, 0x5e}, {0x7f, 0x09, 0x19, 0x29, 0x46},
    {0x46, 0x49, 0x49, 0x49, 0x31}, {0x01, 0x01, 0x7f, 0x01, 0x01}, {0x3f, 0x40, 0x40, 0x40, 0x3f},
    {0x1f, 0x20, 0x40, 0x20, 0x1f}, {0x3f, 0x40, 0x38, 0x40, 0x3f}, {0x63, 0x14, 0x08, 0x14, 0x63},
    {0x07, 0x08, 0x70, 0x08, 0x07}, {0x61, 0x51, 0x49, 0x45, 0x43}};

static const uint8_t digits[10][5] = {
    {62, 81, 73, 69, 62},  {0, 66, 127, 64, 0},  {66, 97, 81, 73, 70}, {33, 65, 69, 75, 49},
    {24, 20, 18, 127, 16}, {39, 69, 69, 69, 57}, {60, 74, 73, 73, 48}, {1, 113, 9, 5, 3},
    {54, 73, 73, 73, 54},  {6, 73, 73, 41, 30}};
static void text(uint16_t *fb, const char *s, unsigned x, unsigned y, unsigned scale,
                 uint16_t color) {
    for (; *s && x + 6 * scale < 800; ++s, x += 6 * scale) {
        unsigned ch = std::toupper((unsigned char)*s);
        const uint8_t *glyph = ch >= 'A' && ch <= 'Z'   ? letters[ch - 'A']
                               : ch >= '0' && ch <= '9' ? digits[ch - '0']
                                                        : nullptr;
        if (!glyph)
            continue;
        for (unsigned c = 0; c < 5; ++c)
            for (unsigned r = 0; r < 7; ++r)
                if (glyph[c] & (1U << r))
                    for (unsigned dx = 0; dx < scale; ++dx)
                        for (unsigned dy = 0; dy < scale; ++dy)
                            fb[(x + c * scale + dx) * 480 + 479 - (y + r * scale + dy)] = color;
    }
}
void yamnet_render(uint16_t *fb, const char *const *labels, const float *scores,
                   const StreamStats *stats) {
    std::memset(fb, 0, 800 * 480 * 2);
    text(fb, "YAMNET SOUND EVENTS", 28, 24, 4, 0x07ff);
    text(fb, "LIVE MICROPHONE   16 KHZ", 28, 68, 2, 0x7bef);
    if (!labels) {
        text(fb, "VALIDATING MODEL AND FRONTEND", 28, 210, 3, 0xffff);
        text(fb, "LISTENING STARTS AFTER SELF TEST", 28, 260, 2, 0x7bef);
        return;
    }
    for (unsigned i = 0; i < 5; ++i) {
        text(fb, labels[i], 28, 120 + 53 * i, 2, 0xffff);
        char pct[20];
        std::snprintf(pct, sizeof(pct), "%u PCT", unsigned(scores[i] * 100 + 0.5f));
        text(fb, pct, 650, 120 + 53 * i, 2, 0x07e0);
    }
    char line[100];
    std::snprintf(line, sizeof(line), "RESULTS %lu   SKIPPED WINDOWS %lu",
                  (unsigned long)stats->inferences, (unsigned long)stats->coalesced_windows);
    text(fb, line, 28, 405, 2, 0x7bef);
    std::snprintf(line, sizeof(line), "AUDIO BLOCKS %lu   ERRORS %ld",
                  (unsigned long)stats->audio_blocks, (long)stats->error);
    text(fb, line, 28, 440, 2, 0x7bef);
}
