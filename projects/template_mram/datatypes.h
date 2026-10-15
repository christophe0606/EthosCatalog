// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "stream_types.h"
#include "app_sizes.h"
typedef struct { StreamBlockInfo info; int16_t samples[MIC_BLOCK_SAMPLES]; } PcmBlock;
typedef struct { StreamBlockInfo info; float samples[MIC_BLOCK_SAMPLES]; } AudioBlock;
typedef struct { StreamBlockInfo info; float samples[SPECTRUM_FFT_SIZE]; } FFTBlock;
typedef struct { StreamBlockInfo info; float samples[SPECTRUM_BINS]; } SpectrumBlock;
#ifdef __cplusplus
static_assert(sizeof(PcmBlock) == 16 + 2*MIC_BLOCK_SAMPLES);
static_assert(sizeof(AudioBlock) == 16 + 4*MIC_BLOCK_SAMPLES);
static_assert(sizeof(FFTBlock) == 16 + 4*SPECTRUM_FFT_SIZE);
static_assert(sizeof(SpectrumBlock) == 16 + 4*SPECTRUM_BINS);
#endif
