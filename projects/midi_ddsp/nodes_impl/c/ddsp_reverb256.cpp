// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "ddsp_reverb256.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#ifdef MIDI_DDSP_CMSIS_DSP
#include "arm_math.h"
#endif

namespace midi_ddsp {
namespace {
constexpr double Tau = 6.283185307179586476925286766559;

// CMSIS packed real spectra: [DC,Nyquist,Re1,Im1,...,Re255,Im255].
// The portable implementation is only for independent host validation.
void fft512(float *input, float *output, bool inverse) {
#ifdef MIDI_DDSP_CMSIS_DSP
    static arm_rfft_fast_instance_f32 instance;
    static bool initialized = false;
    if (!initialized) {
        arm_rfft_fast_init_512_f32(&instance);
        initialized = true;
    }
    arm_rfft_fast_f32(&instance, input, output, inverse ? 1 : 0);
#else
    constexpr unsigned Size = Reverb256::Fft;
    double real[Size], imaginary[Size]{};
    if (!inverse) {
        for (unsigned j = 0; j < Size; ++j) real[j] = input[j];
    } else {
        real[0] = input[0];
        real[Size/2] = input[1];
        for (unsigned j = 1; j < Size/2; ++j) {
            real[j] = real[Size-j] = input[2*j];
            imaginary[j] = input[2*j+1];
            imaginary[Size-j] = -imaginary[j];
        }
    }
    for (unsigned j = 1, k = 0; j < Size; ++j) {
        unsigned bit = Size/2;
        for (; k & bit; bit >>= 1) k ^= bit;
        k ^= bit;
        if (j < k) {
            std::swap(real[j], real[k]);
            std::swap(imaginary[j], imaginary[k]);
        }
    }
    for (unsigned width = 2; width <= Size; width *= 2) {
        for (unsigned base = 0; base < Size; base += width) {
            for (unsigned j = 0; j < width/2; ++j) {
                const double angle = (inverse ? Tau : -Tau)*j/width;
                const double c = std::cos(angle), s = std::sin(angle);
                const unsigned a = base+j, b = a+width/2;
                const double r = real[b]*c-imaginary[b]*s;
                const double i = real[b]*s+imaginary[b]*c;
                real[b] = real[a]-r;
                imaginary[b] = imaginary[a]-i;
                real[a] += r;
                imaginary[a] += i;
            }
        }
    }
    if (inverse) {
        for (unsigned j = 0; j < Size; ++j) output[j] = float(real[j]/Size);
    } else {
        output[0] = float(real[0]);
        output[1] = float(real[Size/2]);
        for (unsigned j = 1; j < Size/2; ++j) {
            output[2*j] = float(real[j]);
            output[2*j+1] = float(imaginary[j]);
        }
    }
#endif
}
}

void Reverb256::init(float *coefficients, float *history,
                     const float *impulse, bool learned) {
    coefficients_ = coefficients;
    history_ = history;
    position_ = 0;
    std::memset(history_, 0, SpectrumFloats*sizeof(float));
    std::memset(overlap_, 0, sizeof(overlap_));
    for (unsigned p = 0; p < Partitions; ++p) {
        float block[Fft]{};
        for (unsigned j = 0; j < Hop; ++j) {
            const unsigned n = p*Hop+j;
            if (n >= 48000) break; // Final partition retains128 real taps.
            block[j] = impulse[n];
            if (learned) {
                if (n == 0) block[j] = 0;
                else if (n >= 16000) block[j] *= float(std::exp(-4.*(n-16000)/31999.));
            }
        }
        fft512(block, coefficients_+p*Fft, false);
    }
}

void Reverb256::process256(const float *dry, float *wet) {
    float block[Fft]{}, sum[Fft]{};
    std::memcpy(block, dry, Hop*sizeof(float));
    fft512(block, history_+position_*Fft, false);
    unsigned history_index = position_;
    for (unsigned p = 0; p < Partitions; ++p) {
        const float *h = coefficients_+p*Fft;
        const float *x = history_+history_index*Fft;
        sum[0] += h[0]*x[0];
        sum[1] += h[1]*x[1];
        for (unsigned j = 2; j < Fft; j += 2) {
            sum[j] += h[j]*x[j]-h[j+1]*x[j+1];
            sum[j+1] += h[j]*x[j+1]+h[j+1]*x[j];
        }
        history_index = history_index == 0 ? Partitions-1 : history_index-1;
    }
    fft512(sum, block, true);
    for (unsigned j = 0; j < Hop; ++j) {
        wet[j] = dry[j]+block[j]+overlap_[j];
        overlap_[j] = block[j+Hop];
    }
    if (++position_ == Partitions) position_ = 0;
}
}
