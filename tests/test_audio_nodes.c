#include "spectrum.h"
#include <stdbool.h>
#include <stddef.h>
#include "arm_math.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// Test-owned declarations: the raw C backends are private to their Stream nodes.
typedef struct { float *data; size_t size, valid; } sliding_buffer_t;
void sliding_buffer_init(sliding_buffer_t *state, float *storage, size_t size);
bool sliding_buffer_push(sliding_buffer_t *state, const float *block, size_t count);
void pcm_to_float(const int16_t *pcm, float *out, size_t count);
void remove_dc(const float *in, float *out, size_t count);
float hann_window_init(float *window, size_t count);
void apply_window(const float *in, const float *window, float *out, size_t count);
int fft_2048_init(arm_rfft_fast_instance_f32 *fft);
void fft_forward(arm_rfft_fast_instance_f32 *fft, float *in, float *packed);
void fft_magnitude(const float *packed, float *out, size_t fft_size, float scale);

static int16_t pcm[SPECTRUM_FFT_SIZE];
static float spectrum_magnitude[SPECTRUM_BINS];
static uint32_t spectrum_peak_hz;
static unsigned bar_height(const uint16_t *column)
{
    unsigned height = 0;
    while (height < SPECTRUM_HEIGHT-SPECTRUM_STRIP && column[height] != 0x0063) ++height;
    return height;
}
static unsigned peak(void)
{
    unsigned p = 1;
    for (unsigned i = 2; i < SPECTRUM_BINS; ++i)
        if (spectrum_magnitude[i] > spectrum_magnitude[p]) p = i;
    return p;
}
static arm_rfft_fast_instance_f32 fft;
static float window[SPECTRUM_FFT_SIZE], history[SPECTRUM_FFT_SIZE];
static float input[SPECTRUM_FFT_SIZE], packed[SPECTRUM_FFT_SIZE];
static float scale;
static int spectrum_init(void)
{
    scale = hann_window_init(window, SPECTRUM_FFT_SIZE);
    return fft_2048_init(&fft);
}
static void spectrum_compute(const int16_t *pcm)
{
    pcm_to_float(pcm, input, SPECTRUM_FFT_SIZE);
    remove_dc(input, input, SPECTRUM_FFT_SIZE);
    apply_window(input, window, input, SPECTRUM_FFT_SIZE);
    fft_forward(&fft, input, packed);
    fft_magnitude(packed, spectrum_magnitude, SPECTRUM_FFT_SIZE, scale);
    spectrum_peak_hz = peak() * SPECTRUM_SAMPLE_RATE / SPECTRUM_FFT_SIZE;
}
static void test_sliding(void)
{
    sliding_buffer_t sliding;
    float block[512];
    sliding_buffer_init(&sliding, history, SPECTRUM_FFT_SIZE);
    for (unsigned n = 0; n < 10; ++n) {
        for (unsigned i = 0; i < 512; ++i) block[i] = (float)(n * 512 + i);
        assert(sliding_buffer_push(&sliding, block, 512) == (n >= 3));
        if (n >= 3) for (unsigned i = 0; i < SPECTRUM_FFT_SIZE; ++i)
            assert(history[i] == (float)((n - 3) * 512 + i));
    }
    sliding_buffer_init(&sliding, history, SPECTRUM_FFT_SIZE);
    assert(!sliding_buffer_push(&sliding, block, 512));
    assert(sliding.valid == 512);
    for (unsigned i = 0; i < SPECTRUM_FFT_SIZE - 512; ++i) assert(history[i] == 0);
}

static void test_pcm_conversion(void)
{
    int16_t samples[257];
    float converted[259];
    /* Cover every signed PCM value, including both endpoints and vector tails. */
    for (unsigned base = 0; base < 65536; base += 257) {
        unsigned count = 65536 - base;
        if (count > 257) count = 257;
        for (unsigned i = 0; i < count; ++i)
            samples[i] = (int16_t)((int)(base + i) - 32768);
        converted[0] = converted[count+1] = 1234.0f;
        pcm_to_float(samples, converted + 1, count);
        assert(converted[0] == 1234.0f && converted[count+1] == 1234.0f);
        for (unsigned i = 0; i < count; ++i)
            assert(converted[i+1] == (float)samples[i] / 32768.0f);
    }
    pcm_to_float(NULL, NULL, 0);
}

static void test_window_and_dc(void)
{
    const unsigned counts[] = {1, 2, 3, 7, 16, 31, SPECTRUM_FFT_SIZE};
    float samples[SPECTRUM_FFT_SIZE], coefficients[SPECTRUM_FFT_SIZE+2];
    float result[SPECTRUM_FFT_SIZE+2], inplace[SPECTRUM_FFT_SIZE];
    for (unsigned t = 0; t < sizeof(counts)/sizeof(counts[0]); ++t) {
        unsigned count = counts[t];
        double sum = 0;
        coefficients[0] = coefficients[count+1] = 1234.0f;
        float amplitude_scale = hann_window_init(coefficients + 1, count);
        assert(coefficients[0] == 1234.0f && coefficients[count+1] == 1234.0f);
        for (unsigned i = 0; i < count; ++i) {
            double expected = 0.5 - 0.5 * cos(6.283185307179586 * i / count);
            assert(fabs(coefficients[i+1] - expected) < 1e-6);
            sum += expected;
            samples[i] = 0.25f + (float)((int)(i % 7) - 3) / 16.0f;
        }
        if (count == 1) assert(amplitude_scale == 0);
        else assert(fabs(amplitude_scale * sum - 2.0) < 1e-5);

        sum = 0;
        for (unsigned i = 0; i < count; ++i) sum += samples[i];
        double mean = sum / count;
        result[0] = result[count+1] = 1234.0f;
        memcpy(inplace, samples, count * sizeof(float));
        remove_dc(samples, result + 1, count);
        remove_dc(inplace, inplace, count);
        assert(result[0] == 1234.0f && result[count+1] == 1234.0f);
        for (unsigned i = 0; i < count; ++i) {
            assert(fabs(result[i+1] - (samples[i] - mean)) < 1e-6);
            assert(inplace[i] == result[i+1]);
        }

        memcpy(inplace, samples, count * sizeof(float));
        apply_window(samples, coefficients + 1, result + 1, count);
        apply_window(inplace, coefficients + 1, inplace, count);
        assert(result[0] == 1234.0f && result[count+1] == 1234.0f);
        for (unsigned i = 0; i < count; ++i) {
            double expected = samples[i] * (0.5 - 0.5 * cos(6.283185307179586 * i / count));
            assert(fabs(result[i+1] - expected) < 1e-6);
            assert(inplace[i] == result[i+1]);
        }
    }
    remove_dc(NULL, NULL, 0);
    assert(hann_window_init(NULL, 0) == 0);
    apply_window(NULL, NULL, NULL, 0);
}

int main(void)
{
    test_sliding();
    test_pcm_conversion();
    test_window_and_dc();
    assert(spectrum_init() == 0);
    spectrum_compute(pcm);
    for (unsigned i = 0; i < SPECTRUM_BINS; ++i) assert(spectrum_magnitude[i] == 0);
    for (unsigned i = 0; i < SPECTRUM_FFT_SIZE; ++i) pcm[i] = 12000;
    spectrum_compute(pcm);
    for (unsigned i = 0; i < SPECTRUM_BINS; ++i) assert(spectrum_magnitude[i] < 1e-6f);
    const unsigned bins[] = {1, 43, 320, 1000};
    for (unsigned t = 0; t < sizeof(bins)/sizeof(bins[0]); ++t) {
        for (unsigned i = 0; i < SPECTRUM_FFT_SIZE; ++i)
            pcm[i] = (int16_t)(16384.0 * sin(6.283185307179586 * bins[t] * i / SPECTRUM_FFT_SIZE));
        spectrum_compute(pcm);
        assert(peak() == bins[t]);
        assert(spectrum_peak_hz == bins[t] * SPECTRUM_SAMPLE_RATE / SPECTRUM_FFT_SIZE);
        assert(fabsf(spectrum_magnitude[bins[t]] - 0.5f) < 0.001f);
    }
    for (unsigned i = 0; i < SPECTRUM_FFT_SIZE; ++i) pcm[i] = (i & 1) ? -16384 : 16384;
    spectrum_compute(pcm);
    assert(fabsf(spectrum_magnitude[SPECTRUM_BINS-1] - 0.5f) < 0.001f);
    size_t pixels = SPECTRUM_WIDTH * SPECTRUM_HEIGHT;
    uint16_t *storage = malloc((pixels + 2) * sizeof(uint16_t));
    assert(storage);
    storage[0] = storage[pixels+1] = 0x1234;
    spectrum_render(storage+1, NULL, true);
    assert(bar_height(storage+1) == 0);
    assert(storage[0] == 0x1234 && storage[pixels+1] == 0x1234);
    spectrum_ui_reset();
    for (unsigned mode = 0; mode < 2; ++mode) {
        memset(spectrum_magnitude, 0, sizeof(spectrum_magnitude));
        spectrum_magnitude[512] = mode ? 0.1f : 0.001f;
        spectrum_render(storage+1, spectrum_magnitude, mode != 0);
        unsigned column = (513 * SPECTRUM_PLOT_WIDTH + SPECTRUM_BINS - 1) / SPECTRUM_BINS - 1;
        unsigned lit = bar_height(storage+1+column*SPECTRUM_HEIGHT);
        assert(lit >= (mode ? 341U : 45U) && lit <= (mode ? 343U : 46U));
        assert(storage[0] == 0x1234 && storage[pixels+1] == 0x1234);
        /* Richer UI-only previews include quiet/high peaks and release markers. */
        spectrum_ui_reset();
        for (unsigned frame = 0; frame < 2; ++frame) {
            for (unsigned bin = 0; bin < SPECTRUM_BINS; ++bin) {
                float b = (float)bin;
                float level = 0.08f + 0.68f*expf(-powf((b-43.0f)/16.0f, 2.0f)) +
                              0.82f*expf(-powf((b-260.0f)/45.0f, 2.0f)) +
                              0.42f*expf(-powf((b-670.0f)/90.0f, 2.0f));
                level *= 0.8f + 0.2f*sinf(b*0.23f);
                if (frame) level *= 0.55f;
                spectrum_magnitude[bin] = mode ? powf(10.0f, (level*80.0f-80.0f)/20.0f)
                                               : level / SPECTRUM_LINEAR_GAIN;
            }
            spectrum_render(storage+1, spectrum_magnitude, mode != 0);
        }
        FILE *f = fopen(mode ? "out/audio-spectrum-log.ppm" : "out/audio-spectrum-linear.ppm", "wb");
        assert(f);
        fprintf(f, "P6\n800 480\n255\n");
        for (unsigned y = 0; y < SPECTRUM_HEIGHT; ++y)
            for (unsigned x = 0; x < SPECTRUM_WIDTH; ++x) {
                uint16_t c = storage[1+x*SPECTRUM_HEIGHT+SPECTRUM_HEIGHT-1-y];
                unsigned char rgb[] = {(unsigned char)(((c>>11)&31)*255/31),
                    (unsigned char)(((c>>5)&63)*255/63), (unsigned char)((c&31)*255/31)};
                fwrite(rgb, 1, 3, f);
            }
        fclose(f);
    }
    /* Loud input must saturate every column at the plot boundary, including
     * the first and last columns, without touching the indicator strip. */
    spectrum_display_gain = 2.0f;
    for (unsigned mode = 0; mode < 2; ++mode) {
        for (unsigned bin = 0; bin < SPECTRUM_BINS; ++bin)
            spectrum_magnitude[bin] = 10.0f;
        spectrum_render(storage + 1, spectrum_magnitude, mode != 0);
        for (unsigned x = 0; x < SPECTRUM_PLOT_WIDTH; ++x) {
            for (unsigned y = SPECTRUM_STRIP; y < SPECTRUM_HEIGHT; ++y)
                assert(storage[1+x*SPECTRUM_HEIGHT+SPECTRUM_HEIGHT-1-y] != 0x0063);
            assert(storage[1+x*SPECTRUM_HEIGHT] == 0x3E51);
            assert(storage[1+x*SPECTRUM_HEIGHT+280] == 0xF58A);
            assert(storage[1+x*SPECTRUM_HEIGHT+430] == 0xF34D);
            assert(storage[1+x*SPECTRUM_HEIGHT+SPECTRUM_HEIGHT-SPECTRUM_STRIP] != 0xF34D);
        }
        assert(storage[0] == 0x1234 && storage[pixels+1] == 0x1234);
    }
    /* Slider motion changes height in both modes, without toggling the mode.
     * A 40% baseline should become 20% at minimum and 80% at maximum. */
    for (unsigned mode = 0; mode < 2; ++mode) {
        for (unsigned endpoint = 0; endpoint < 2; ++endpoint) {
            assert(!spectrum_touch(SPECTRUM_WIDTH-1U,
                                   endpoint ? 0U : SPECTRUM_HEIGHT-1U, true));
            assert(spectrum_display_gain == (endpoint ? 2.0f : 0.5f));
            for (unsigned bin = 0; bin < SPECTRUM_BINS; ++bin)
                spectrum_magnitude[bin] = mode ? powf(10.0f, -48.0f / 20.0f) : 0.004f;
            spectrum_render(storage+1, spectrum_magnitude, mode != 0);
            unsigned lit = bar_height(storage+1);
            unsigned expected = endpoint ? 364U : 91U;
            assert(lit >= expected-1U && lit <= expected+1U);
            /* Even when the spectrum saturates, the slider's outer edge
             * remains its own background, not a spectrum column. */
            assert(storage[1+(SPECTRUM_WIDTH-1U)*SPECTRUM_HEIGHT] == 0x08A4);
            assert(storage[0] == 0x1234 && storage[pixels+1] == 0x1234);
        }
    }
    float previous_gain = spectrum_display_gain;
    spectrum_touch_release();
    assert(spectrum_touch(0, 0, true));
    assert(!spectrum_touch(0, 0, false));
    assert(!spectrum_touch(SPECTRUM_WIDTH, SPECTRUM_HEIGHT, true));
    assert(spectrum_display_gain == previous_gain);
    spectrum_display_gain = 1.0f;
    /* Sudden sound rises quickly, releases slowly, and leaves a fading marker
     * that eventually disappears. Check this for both display scales. */
    for (unsigned mode = 0; mode < 2; ++mode) {
        spectrum_ui_reset();
        memset(spectrum_magnitude, 0, sizeof(spectrum_magnitude));
        spectrum_render(storage+1, spectrum_magnitude, mode != 0);
        spectrum_magnitude[0] = mode ? 0.1f : 0.0075f;
        spectrum_render(storage+1, spectrum_magnitude, mode != 0);
        unsigned attack = bar_height(storage+1);
        assert(attack > 260U && attack < 300U);
        spectrum_magnitude[0] = 0;
        spectrum_render(storage+1, spectrum_magnitude, mode != 0);
        unsigned release = bar_height(storage+1);
        assert(release < attack && release > 200U);
        assert(storage[1+attack] != 0x0063); /* retained peak */
        uint16_t bright = storage[1+attack];
        spectrum_render(storage+1, spectrum_magnitude, mode != 0);
        assert(storage[1+attack] != bright); /* fading */
        for (unsigned frame = 0; frame < 60U; ++frame) spectrum_render(storage+1, spectrum_magnitude, mode != 0);
        assert(bar_height(storage+1) == 0U);
        for (unsigned row = 0; row < SPECTRUM_HEIGHT-SPECTRUM_STRIP; ++row)
            assert(storage[1+row] == 0x0063);
        assert(storage[0] == 0x1234 && storage[pixels+1] == 0x1234);
    }
    free(storage);
    puts("Audio checks passed: full Q15 range, periodic Hann, DC removal, in-place processing, spectrum, UI bounds.");
}

