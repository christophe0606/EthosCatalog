/* Local modifications for this demo (2026-10-07): Alif Ensemble 2.2.1 OSPI pin/reset preparation and model storage adapter.
 * Copyright (c) 2026 Arm Limited or its affiliates. All rights reserved.
 * New Arm contributions were generated with AI assistance, under Apache-2.0.
 * Reused vendor code retains the original licenses/notices below;
 * this notice does not relicense it. See THIRD_PARTY_NOTICES.md.
 */

/* OSPI1 pin/reset setup adapted from Ensemble 2.2.1 app_issi_flash.c.
 * Copyright (C) 2024 Alif Semiconductor - All Rights Reserved.
 * Use, distribution and modification permitted under the Alif Semiconductor
 * Software License Agreement: https://alifsemi.com/license
 */
#include <stdbool.h>
#include "RTE_Components.h"
#include CMSIS_device_header
#include "Driver_IO.h"
#include "pinconf.h"
#include "sys_utils.h"
#include "ospi_xip_user.h"
#include "setup_flash_xip.h"
extern int e8_setup_flash_xip(void);

#if OSPI_XIP_INSTANCE != 1
#error This board's external model storage requires OSPI1
#endif

extern ARM_DRIVER_GPIO ARM_Driver_GPIO_(15);
static const struct { uint8_t port, pin, function; uint32_t pad; } pads[] = {
#define DATA_PAD (PADCTRL_OUTPUT_DRIVE_STRENGTH_12MA | PADCTRL_SLEW_RATE_FAST | PADCTRL_READ_ENABLE)
    {OSPI1_D0_PORT, OSPI1_D0_PIN, OSPI1_D0_PIN_FUNCTION, DATA_PAD},
    {OSPI1_D1_PORT, OSPI1_D1_PIN, OSPI1_D1_PIN_FUNCTION, DATA_PAD},
    {OSPI1_D2_PORT, OSPI1_D2_PIN, OSPI1_D2_PIN_FUNCTION, DATA_PAD},
    {OSPI1_D3_PORT, OSPI1_D3_PIN, OSPI1_D3_PIN_FUNCTION, DATA_PAD},
    {OSPI1_D4_PORT, OSPI1_D4_PIN, OSPI1_D4_PIN_FUNCTION, DATA_PAD},
    {OSPI1_D5_PORT, OSPI1_D5_PIN, OSPI1_D5_PIN_FUNCTION, DATA_PAD},
    {OSPI1_D6_PORT, OSPI1_D6_PIN, OSPI1_D6_PIN_FUNCTION, DATA_PAD},
    {OSPI1_D7_PORT, OSPI1_D7_PIN, OSPI1_D7_PIN_FUNCTION, DATA_PAD},
    {OSPI1_RXDS_PORT, OSPI1_RXDS_PIN, OSPI1_RXDS_PIN_FUNCTION, DATA_PAD},
    {OSPI1_SCLK_PORT, OSPI1_SCLK_PIN, OSPI1_SCLK_PIN_FUNCTION, PADCTRL_OUTPUT_DRIVE_STRENGTH_12MA | PADCTRL_SLEW_RATE_FAST},
    {OSPI1_CS_PORT, OSPI1_CS_PIN, OSPI1_CS_PIN_FUNCTION, PADCTRL_OUTPUT_DRIVE_STRENGTH_12MA},
    {OSPI1_SCLKN_PORT, OSPI1_SCLKN_PIN, OSPI1_SCLKN_PIN_FUNCTION, PADCTRL_OUTPUT_DRIVE_STRENGTH_12MA},
};

/* Physical flash preparation is also needed before standalone programming;
 * it does not configure the controller protocol or enable XIP.
 */
int model_storage_prepare_programming(void)
{
    for (unsigned i = 0; i < sizeof(pads)/sizeof(pads[0]); ++i)
        if (pinconf_set(pads[i].port, pads[i].pin, pads[i].function, pads[i].pad)) return -301;
    ARM_DRIVER_GPIO *gpio = &ARM_Driver_GPIO_(15);
    if (gpio->Initialize(7, NULL) != ARM_DRIVER_OK ||
        gpio->PowerControl(7, ARM_POWER_FULL) != ARM_DRIVER_OK ||
        gpio->SetDirection(7, GPIO_PIN_DIRECTION_OUTPUT) != ARM_DRIVER_OK ||
        gpio->SetValue(7, GPIO_PIN_OUTPUT_STATE_LOW) != ARM_DRIVER_OK) return -302;
    sys_busy_loop_us(10);
    if (gpio->SetValue(7, GPIO_PIN_OUTPUT_STATE_HIGH) != ARM_DRIVER_OK) return -302;
    sys_busy_loop_us(1000);
    return 0;
}

int model_storage_init(void)
{
    int error = model_storage_prepare_programming();
    if (error) return error;
    if (e8_setup_flash_xip()) return -303;
    SCB_CleanInvalidateDCache();
    __DSB();
    return 0;
}
