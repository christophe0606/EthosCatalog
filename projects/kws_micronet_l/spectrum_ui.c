/* Adapted from the Spectrogram example.
 * Copyright (C) 2023 Alif Semiconductor - All Rights Reserved.
 * Use, distribution and modification permitted under the Alif Semiconductor
 * Software License Agreement: https://alifsemi.com/license
 */
#include "spectrum.h"
#include "arm_math.h"
#include <math.h>
#include <string.h>
#define RGB(r,g,b) ((uint16_t)(((r)>>3)<<11 | ((g)>>2)<<5 | ((b)>>3)))
#define PLOT_HEIGHT (SPECTRUM_HEIGHT - SPECTRUM_STRIP)
#define BACKGROUND RGB(7,13,25)
#define PANEL RGB(13,23,38)
float spectrum_display_gain = 1.0f;
static float smoothed[SPECTRUM_PLOT_WIDTH];
static uint16_t peaks[SPECTRUM_PLOT_WIDTH];
static uint8_t peak_age[SPECTRUM_PLOT_WIDTH];
static q15_t palette[PLOT_HEIGHT];
static bool palette_ready, history_ready, previous_log, slider_active;
static float previous_gain;
static unsigned slider_glow;

void spectrum_ui_reset(void)
{
    memset(smoothed, 0, sizeof(smoothed));
    memset(peaks, 0, sizeof(peaks));
    memset(peak_age, 0, sizeof(peak_age));
    history_ready = false;
    slider_active = false;
    slider_glow = 0;
}

void spectrum_touch_release(void)
{
    slider_active = false;
}

static uint16_t blend(unsigned r0, unsigned g0, unsigned b0,
                      unsigned r1, unsigned g1, unsigned b1, unsigned step, unsigned total)
{
    unsigned r = (r0 * (total-step) + r1 * step) / total;
    unsigned g = (g0 * (total-step) + g1 * step) / total;
    unsigned b = (b0 * (total-step) + b1 * step) / total;
    return RGB(r,g,b);
}

static void prepare_palette(void)
{
    if (palette_ready) return;
    const unsigned green = PLOT_HEIGHT / 2U, orange = PLOT_HEIGHT * 4U / 5U;
    const unsigned transition = 18U;
    for (unsigned row = 0; row < PLOT_HEIGHT; ++row) {
        uint16_t color;
        if (row < green) color = RGB(58,202,139);
        else if (row < green + transition)
            color = blend(58,202,139, 242,179,85, row-green, transition);
        else if (row < orange) color = RGB(242,179,85);
        else if (row < orange + transition)
            color = blend(242,179,85, 242,105,106, row-orange, transition);
        else color = RGB(242,105,106);
        palette[row] = (q15_t)color;
    }
    palette_ready = true;
}

bool spectrum_touch(unsigned x, unsigned y, bool new_contact)
{
    if (x >= SPECTRUM_WIDTH || y >= SPECTRUM_HEIGHT) return false;
    if (x < SPECTRUM_PLOT_WIDTH) return new_contact && !slider_active;
    slider_active = true;
    if (y < SPECTRUM_SLIDER_TOP) y = SPECTRUM_SLIDER_TOP;
    if (y > SPECTRUM_SLIDER_BOTTOM) y = SPECTRUM_SLIDER_BOTTOM;
    spectrum_display_gain = 0.5f + 1.5f * (float)(SPECTRUM_SLIDER_BOTTOM - y) /
                          (float)(SPECTRUM_SLIDER_BOTTOM - SPECTRUM_SLIDER_TOP);
    return false;
}

static void rectangle(uint16_t *fb, unsigned x, unsigned y,
                      unsigned width, unsigned height, uint16_t color)
{
    if (x >= SPECTRUM_WIDTH || y >= SPECTRUM_HEIGHT) return;
    if (width > SPECTRUM_WIDTH-x) width = SPECTRUM_WIDTH-x;
    if (height > SPECTRUM_HEIGHT-y) height = SPECTRUM_HEIGHT-y;
    for (unsigned col = x; col < x + width; ++col)
        arm_fill_q15((q15_t)color,
                     (q15_t *)(fb + col * SPECTRUM_HEIGHT + SPECTRUM_HEIGHT - y - height),
                     height);
}

static void rounded_rectangle(uint16_t *fb, unsigned x, unsigned y,
                              unsigned width, unsigned height, unsigned radius, uint16_t color)
{
    for (unsigned row = 0; row < height; ++row) {
        unsigned edge = row < height/2U ? row : height-1U-row;
        unsigned inset = 0;
        if (edge < radius) {
            unsigned dy = radius-1U-edge;
            while (inset < radius &&
                   (radius-inset)*(radius-inset) + dy*dy > radius*radius) ++inset;
        }
        rectangle(fb, x+inset, y+row, width-2U*inset, 1U, color);
    }
}

/* Landscape coordinates map to the native portrait 480 x 800 buffer. */
static void pixel(uint16_t *fb, unsigned x, unsigned y, uint16_t color)
{
    fb[x * SPECTRUM_HEIGHT + (SPECTRUM_HEIGHT - 1U - y)] = color;
}

static void label(uint16_t *fb, unsigned x, unsigned y, unsigned scale,
                  const char *s, uint16_t color)
{
    static const uint8_t glyphs[][7] = {
        {16,16,16,16,16,16,31}, /* L */
        {14,4,4,4,4,4,14},     /* I */
        {17,25,21,19,17,17,17},/* N */
        {31,16,16,30,16,16,31},/* E */
        {14,17,17,31,17,17,17},/* A */
        {30,17,17,30,20,18,17},/* R */
        {14,17,17,17,17,17,14},/* O */
        {14,17,16,23,17,17,15},/* G */
        {14,17,19,21,25,17,14},/* 0 */
        {4,12,4,4,4,4,14},     /* 1 */
        {14,17,1,2,4,8,31},    /* 2 */
        {2,6,10,18,31,2,2},    /* 4 */
        {14,16,16,30,17,17,14},/* 6 */
        {14,17,17,14,17,17,14},/* 8 */
        {17,18,20,24,20,18,17},/* K */
        {17,17,17,31,17,17,17},/* H */
        {31,1,2,4,8,16,31}     /* Z */
    };
    const char *letters = "LINEAROG012468KHZ";
    for (; *s; ++s, x += 6U*scale) {
        unsigned g = 0;
        while (letters[g] && letters[g] != *s) ++g;
        if (!letters[g]) continue;
        for (unsigned r = 0; r < 7; ++r)
            for (unsigned c = 0; c < 5; ++c)
                if (glyphs[g][r] & (1U << (4U-c)))
                    for (unsigned dy = 0; dy < scale; ++dy)
                        for (unsigned dx = 0; dx < scale; ++dx)
                            pixel(fb, x+c*scale+dx, y+r*scale+dy, color);
    }
}

void spectrum_render(uint16_t *fb, const float *magnitudes, bool logarithmic)
{
    const unsigned plot_height = SPECTRUM_HEIGHT - SPECTRUM_STRIP;
    const float gain = fminf(2.0f, fmaxf(0.5f, spectrum_display_gain));
    prepare_palette();
    /* A scale change takes effect immediately; subsequent audio frames have
     * 50 ms attack and about 250 ms release at the fixed 20 fps cadence. */
    bool reset = !history_ready || logarithmic != previous_log || gain != previous_gain;
    for (unsigned x = 0; x < SPECTRUM_PLOT_WIDTH; ++x) {
        /* Keep the entire frequency range in the width left of the slider. */
        unsigned first = x * SPECTRUM_BINS / SPECTRUM_PLOT_WIDTH;
        unsigned end = (x+1U) * SPECTRUM_BINS / SPECTRUM_PLOT_WIDTH;
        float a = 0.0f;
        if (magnitudes)
            for (unsigned bin = first; bin < end; ++bin)
                if (magnitudes[bin] > a) a = magnitudes[bin];
        /* Display-only gain makes quiet microphone signals visible in linear
         * mode. Clip before converting to a height or filling the buffer. */
        float level = logarithmic ? (20.0f * log10f(fmaxf(a, 0.0001f)) + 80.0f) / 80.0f
                                  : a * SPECTRUM_LINEAR_GAIN;
        level = fminf(1.0f, fmaxf(0.0f, level * gain));
        float target = level * (float)plot_height;
        if (reset) {
            smoothed[x] = target;
            peaks[x] = 0;
            peak_age[x] = 0;
        } else {
            float rate = target > smoothed[x] ? 0.8f : 0.18f;
            smoothed[x] += rate * (target - smoothed[x]);
        }
        unsigned height = (unsigned)fminf((float)plot_height, fmaxf(0.0f, smoothed[x]));
        /* In portrait memory, each landscape column is contiguous. Use
         * optimized DSP fills so Debug's unoptimized UI stays within 50 ms. */
        q15_t *column = (q15_t *)(fb + x * SPECTRUM_HEIGHT);
        memcpy(column, palette, height * sizeof(q15_t));
        arm_fill_q15((q15_t)BACKGROUND, column + height, plot_height - height);
        arm_fill_q15((q15_t)PANEL, column + plot_height, SPECTRUM_STRIP);
        /* A peak stays above the falling bar and fades away over 1.2 s.
         * Bound both marker pixels to the plot, including full-scale peaks. */
        if (height >= peaks[x] || peak_age[x] >= 24U) {
            peaks[x] = (uint16_t)height;
            peak_age[x] = 0;
        } else ++peak_age[x];
        if (peaks[x] > height + 3U && peak_age[x] < 24U) {
            unsigned at = peaks[x] > plot_height-2U ? plot_height-2U : peaks[x];
            uint16_t color = blend(181,208,224, 7,13,25, peak_age[x], 24U);
            arm_fill_q15((q15_t)color, column + at, 2U);
        }
    }
    history_ready = true;
    previous_log = logarithmic;
    previous_gain = gain;
    /* Slider has a separate touch area, with no scale markings or value. */
    rectangle(fb, SPECTRUM_PLOT_WIDTH, 0, SPECTRUM_SLIDER_WIDTH,
              SPECTRUM_HEIGHT, PANEL);
    const unsigned slider_x = SPECTRUM_PLOT_WIDTH + SPECTRUM_SLIDER_WIDTH / 2U;
    const unsigned slider_y = SPECTRUM_SLIDER_BOTTOM - (unsigned)
        ((gain - 0.5f) / 1.5f * (SPECTRUM_SLIDER_BOTTOM - SPECTRUM_SLIDER_TOP));
    rounded_rectangle(fb, slider_x - 3U, SPECTRUM_SLIDER_TOP, 6U,
                      SPECTRUM_SLIDER_BOTTOM - SPECTRUM_SLIDER_TOP + 1U, 3U, RGB(43,61,80));
    rounded_rectangle(fb, slider_x - 3U, slider_y, 6U,
                      SPECTRUM_SLIDER_BOTTOM-slider_y+1U, 3U, RGB(66,146,153));
    if (slider_active) slider_glow = 8U;
    else if (slider_glow) --slider_glow;
    if (slider_glow)
        rounded_rectangle(fb, slider_x-22U, slider_y-14U, 44U, 28U, 12U,
                          blend(13,23,38, 35,83,95, slider_glow, 8U));
    rounded_rectangle(fb, slider_x-18U, slider_y-8U, 36U, 20U, 9U, RGB(5,10,20));
    rounded_rectangle(fb, slider_x-18U, slider_y-10U, 36U, 20U, 9U,
                      slider_active ? RGB(213,245,238) : RGB(182,208,218));
    /* Frequency annotations share the existing strip; no magnitude axes. */
    rounded_rectangle(fb, 60U, 3U, 50U, 18U, 7U, RGB(28,49,63));
    label(fb, logarithmic ? 76U : 67U, 8U, 1U, logarithmic ? "LOG" : "LINEAR", RGB(164,223,211));
    label(fb, 4U, 9U, 1U, "0HZ", RGB(126,148,166));
    label(fb, SPECTRUM_PLOT_WIDTH / 4U - 12U, 9U, 1U, "6KHZ", RGB(126,148,166));
    label(fb, SPECTRUM_PLOT_WIDTH / 2U - 15U, 9U, 1U, "12KHZ", RGB(126,148,166));
    label(fb, SPECTRUM_PLOT_WIDTH * 3U / 4U - 15U, 9U, 1U, "18KHZ", RGB(126,148,166));
    label(fb, SPECTRUM_PLOT_WIDTH - 34U, 9U, 1U, "24KHZ", RGB(126,148,166));
}
