/*---------------------------------------------------------------------------
 * Copyright (c) 2025 Arm Limited (or its affiliates).
 * All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the License); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an AS IS BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *---------------------------------------------------------------------------*/

#ifndef MAIN_H_
#define MAIN_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Prototypes */
extern int app_main(void);
/* Optional project hook after board initialization, before the RTOS starts. */
extern int app_prepare(void);
extern volatile int32_t app_result;
extern int shield_setup(void);
extern int stdio_init(void);

#if defined(ETHOSU_ARCH)
extern void ethos_setup (void);

/* Filled by the driver callbacks; cycles exclude host and interrupt latency. */
typedef struct {
    uint64_t cycles;
    uint64_t active_cycles;
    uint32_t invocations;
    uint32_t overflow;
    uint32_t weights_address;
    uint32_t weights_size;
    uint32_t arena_address;
    uint32_t arena_size;
} ethos_benchmark_t;
extern volatile ethos_benchmark_t ethos_benchmark;
extern void ethos_benchmark_reset(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* MAIN_H_ */
