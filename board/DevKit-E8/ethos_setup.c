/* Local modifications for this demo (2026-10-07): Vendor NPU initialization and duplicate clock/interrupt integration.
 * Copyright (c) 2026 Arm Limited or its affiliates. All rights reserved.
 * New Arm contributions were generated with AI assistance, under Apache-2.0.
 * Reused vendor code retains the original licenses/notices below;
 * this notice does not relicense it. See THIRD_PARTY_NOTICES.md.
 */

/*---------------------------------------------------------------------------
 * Copyright (c) 2025 Arm Limited (or its affiliates). All rights reserved.
 *
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

 #include <stdio.h>

#include "RTE_Components.h"
#include  CMSIS_device_header

#include "ethosu_driver.h"
#include "pmu_ethosu.h"
#include "main.h"

#if defined(ETHOSU85)
/* Define Ethos-U NPU base address and interrupt handler */
#define ETHOSU_BASE_ADDR            NPU_HG_BASE
#define ETHOSU_IRQ_HANDLER          IRQ366_Handler /* NPU_HG_IRQHandler */
#define ETHOSU_IRQ_IRQn             366            /* NPU_HG_IRQn       */

/* Define Ethos-U NPU cache buffer size */
#ifndef ETHOS_CACHE_BUF_SIZE
#define ETHOS_CACHE_BUF_SIZE        0
#endif

/* Define Ethos-U cache buffer alignment */
#ifndef ETHOS_CACHE_BUF_ALIGNMENT
#define ETHOS_CACHE_BUF_ALIGNMENT   32
#endif

/* Define Ethos-U NPU cache buffer attributes */
#ifndef ETHOS_CACHE_BUF_ATTRIBUTES
#define ETHOS_CACHE_BUF_ATTRIBUTES  __attribute__((section("ethos_cache_buf"), aligned(ETHOS_CACHE_BUF_ALIGNMENT)))
#endif

#else /* Ethos-U55 */
/* Define Ethos-U NPU base address and interrupt handler */
#define ETHOSU_BASE_ADDR            NPU_HP_BASE
#define ETHOSU_IRQ_HANDLER          NPU_HP_IRQHandler
#define ETHOSU_IRQ_IRQn             NPU_HP_IRQ_IRQn
#endif /* ETHOSU85 */

/* Define Ethos-U NPU security mode */
#ifndef ETHOS_SECURE_ENABLE
#define ETHOS_SECURE_ENABLE         1
#endif

/* Define Ethos-U NPU privilege mode */
#ifndef ETHOS_PRIVILEGE_ENABLE
#define ETHOS_PRIVILEGE_ENABLE      1
#endif

/* Ethos NPU driver instance. */
static struct ethosu_driver EthosDriver;
volatile uint32_t npu_interrupts;
volatile int32_t npu_error;
volatile ethos_benchmark_t ethos_benchmark;

void ethos_benchmark_reset(void) {
    ethos_benchmark.cycles = 0;
    ethos_benchmark.active_cycles = 0;
    ethos_benchmark.invocations = 0;
    ethos_benchmark.overflow = 0;
}

/* U85 TRM 102685 r0p0, PMCCNTR_CFG and PMEVTYPER: start at NPU_ACTIVE,
 * stop at NPU_IDLE. This avoids counting the CPU setup/IRQ completion time. */
void ethosu_inference_begin(struct ethosu_driver *drv, void *user_arg) {
    (void)user_arg;
    if (drv->job.num_base_addr >= 2) {
        ethos_benchmark.weights_address = (uint32_t)drv->job.base_addr[0];
        ethos_benchmark.weights_size = drv->job.base_addr_size[0];
        ethos_benchmark.arena_address = (uint32_t)drv->job.base_addr[1];
        ethos_benchmark.arena_size = drv->job.base_addr_size[1];
    }
    ETHOSU_PMU_Enable(drv);
    ETHOSU_PMU_CNTR_Disable(drv, ETHOSU_PMU_CCNT_Msk | ETHOSU_PMU_CNT1_Msk);
    ETHOSU_PMU_PMCCNTR_CFG_Set_Start_Event(drv, ETHOSU_PMU_NPU_ACTIVE);
    ETHOSU_PMU_PMCCNTR_CFG_Set_Stop_Event(drv, ETHOSU_PMU_NPU_IDLE);
    ETHOSU_PMU_Set_EVTYPER(drv, 0, ETHOSU_PMU_NPU_ACTIVE);
    ETHOSU_PMU_CYCCNT_Reset(drv);
    ETHOSU_PMU_EVCNTR_ALL_Reset(drv);
    ETHOSU_PMU_Set_CNTR_OVS(drv, ETHOSU_PMU_CCNT_Msk | ETHOSU_PMU_CNT1_Msk);
    ETHOSU_PMU_CNTR_Enable(drv, ETHOSU_PMU_CCNT_Msk | ETHOSU_PMU_CNT1_Msk);
}

void ethosu_inference_end(struct ethosu_driver *drv, void *user_arg) {
    (void)user_arg;
    ETHOSU_PMU_CNTR_Disable(drv, ETHOSU_PMU_CCNT_Msk | ETHOSU_PMU_CNT1_Msk);
    ethos_benchmark.cycles += ETHOSU_PMU_Get_CCNTR(drv);
    ethos_benchmark.active_cycles += ETHOSU_PMU_Get_EVCNTR(drv, 0);
    ethos_benchmark.overflow |= ETHOSU_PMU_Get_CNTR_OVS(drv);
    ++ethos_benchmark.invocations;
    ETHOSU_PMU_Disable(drv);
}

#if defined(ETHOSU85)
static uint8_t ethos_cache[ETHOS_CACHE_BUF_SIZE] ETHOS_CACHE_BUF_ATTRIBUTES;
#endif

/*
  Ethos NPU interrupt handler.
*/
void ETHOSU_IRQ_HANDLER(void) {
    ++npu_interrupts;
    ethosu_irq_handler(&EthosDriver);
}

/*
  Initialize the Ethos NPU driver.
*/
void ethos_setup (void) {
    void *const ethos_base_addr = (void *)ETHOSU_BASE_ADDR;
    int rval;
    #ifdef DEBUG
    struct ethosu_hw_info hw_info;
    #endif

    /*  Initialize Ethos-U NPU driver. */
    rval = ethosu_init(&EthosDriver,           /* Ethos-U device driver */
                      ethos_base_addr,         /* Ethos-U base address  */
                      #if (ETHOS_CACHE_BUF_SIZE > 0)
                      ethos_cache,             /* Cache memory pointer  */
                      sizeof(ethos_cache),     /* Cache memory size     */
                      #else
                      0,                       /* Cache memory pointer  */
                      0,                       /* Cache memory size     */
                      #endif
                      ETHOS_SECURE_ENABLE,     /* Secure enable         */
                      ETHOS_PRIVILEGE_ENABLE); /* Privileged mode       */
    npu_error = rval;
    if (rval != 0) {
        #ifdef DEBUG
          printf("Failed to initialize Arm Ethos-U driver\n");
        #endif
        /* Early return on error */
        return;
    }

    NVIC_EnableIRQ(ETHOSU_IRQ_IRQn);

    #ifdef DEBUG
    ethosu_get_hw_info(&EthosDriver, &hw_info);

    printf("Ethos-U version info:\n");
    printf("\tArch:       v%u.%u.%u\n", hw_info.version.arch_major_rev,
                                        hw_info.version.arch_minor_rev,
                                        hw_info.version.arch_patch_rev);
    printf("\tMACs/cc:    %u\n", (uint32_t)(1 << hw_info.cfg.macs_per_cc));
    printf("\tCmd stream: v%u\n", hw_info.cfg.cmd_stream_version);
    #endif
}
