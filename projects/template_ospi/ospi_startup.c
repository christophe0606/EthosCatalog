/* OSPI preparation adapted from Blinky_M55_HP, chess branch,
 * M55_HP/main.c at 37c18e4eb6e32f46d467aafc9d3188f4c5b1f519.
 * Copyright (c) 2026 Arm Limited or its affiliates. All rights reserved.
 * New Arm contributions are licensed under Apache-2.0.
 *
 * Reused Alif sample initialization retains its original notice:
 * Copyright (C) 2023 Alif Semiconductor - All Rights Reserved.
 * Use, distribution and modification permitted under the Alif Semiconductor
 * Software License Agreement: https://alifsemi.com/license
 */
#include <stdbool.h>
#include <stdint.h>
#include "RTE_Components.h"
#include CMSIS_device_header
#include "main.h"
#include "se_services_port.h"
#include "setup_flash_xip.h"
#include "sys_ctrl_ospi.h"

extern int model_storage_prepare_programming(void);
extern int model_storage_init(void);

volatile uint32_t app_stage;
volatile int32_t app_error;
volatile uint32_t app_service_error;
volatile uint32_t model_programming_ready;

/* Stable breakpoint locations. Both paths stay in internal memory. */
__attribute__((noinline, noreturn)) void model_programming_wait(void)
{
    for (;;) __WFI();
}

__attribute__((noinline, noreturn)) static void preparation_failed(int32_t error)
{
    app_error = error;
    app_result = error;
    app_stage = 0xFFFFFFFFU;
    for (;;) __WFI();
}

static void check_service(uint32_t status, uint32_t service_error)
{
    app_service_error = service_error;
    if (status != SERVICES_REQ_SUCCESS || service_error != 0U) {
        preparation_failed(status != SERVICES_REQ_SUCCESS ? (int32_t)status : -1);
    }
}

/* Overrides the board's weak hook, after board initialization and before
 * osThreadNew/osKernelStart. ProgramModel never starts the scheduler or reads
 * the model. Debug/Release instead enable XIP before app_main uses the model.
 */
int app_prepare(void)
{
    uint32_t service_error = 0U;
    uint32_t status;
    run_profile_t profile = {0};
    app_stage = 1U;
    status = SERVICES_clocks_enable_clock(se_services_s_handle, CLKEN_CLK_100M,
                                         true, &service_error);
    check_service(status, service_error);
    status = SERVICES_clocks_enable_clock(se_services_s_handle, CLKEN_HFOSC,
                                         true, &service_error);
    check_service(status, service_error);
    status = SERVICES_get_run_cfg(se_services_s_handle, &profile, &service_error);
    check_service(status, service_error);
    profile.memory_blocks |= MRAM_MASK | SRAM0_MASK | SRAM1_MASK;
    status = SERVICES_set_run_cfg(se_services_s_handle, &profile, &service_error);
    check_service(status, service_error);
    app_stage = 2U;

#ifdef MODEL_PROGRAMMING_MODE
    /* Enable the controller clock before the XIP flag is read. A core reset
     * does not reliably clear XIP retained from an earlier application.
     */
#if SOC_FEAT_OSPI_HAS_CLK_ENABLE
    enable_ospi_clk(OSPI_INSTANCE_1);
#endif
    int error = model_storage_prepare_programming();
    if (error) preparation_failed(error);
    if (flash_xip_enabled()) preparation_failed(-305);

    /* Match the reference's quiescent loader entry. Its RAM stack must not
     * inherit the application's DTCM stack limits or MPU/cache policy.
     */
    __disable_irq();
    SysTick->CTRL = 0U;
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;
    SCB_DisableDCache();
    SCB_DisableICache();
    __DMB();
    MPU->CTRL = 0U;
    __set_MSPLIM(0U);
    __set_PSPLIM(0U);
    __DSB();
    __ISB();
    app_error = 0;
    app_stage = 5U;
    model_programming_ready = 1U;
    model_programming_wait();
#else
    int error = model_storage_init();
    if (error) preparation_failed(error);
    app_stage = 3U;
    return 0;
#endif
}

#ifdef MODEL_PROGRAMMING_MODE
/* The board references app_main for its normal RTOS path. Reaching it in
 * ProgramModel would be a startup error; no inference/runtime is linked.
 */
int app_main(void)
{
    preparation_failed(-306);
}
#endif
