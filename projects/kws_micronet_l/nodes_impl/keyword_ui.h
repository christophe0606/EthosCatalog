// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
const char *kws_label(unsigned index);
void keyword_render(uint16_t *frame,unsigned word,unsigned alpha);
#ifdef __cplusplus
}
#endif
