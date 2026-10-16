// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
int kws_forward(void *module, const float *features, int8_t *scores, uint32_t *ticks);
int kws_forward_int8(void *module, const int8_t *features, int8_t *scores, uint32_t *ticks);
unsigned kws_classify(const int8_t *scores, float *probability);
#ifdef __cplusplus
}
#endif
