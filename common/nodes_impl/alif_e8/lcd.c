/* Adapted from the Spectrogram example.
 * Copyright (C) 2023 Alif Semiconductor - All Rights Reserved.
 * Use, distribution and modification permitted under the Alif Semiconductor
 * Software License Agreement: https://alifsemi.com/license
 */
/* New Arm contributions: split submission/release and region cache cleaning.
 * Copyright 2026 Arm Limited and/or its affiliates.
 * New Arm contributions are licensed under Apache-2.0; this does not relicense
 * the adapted Alif material above. */
#include "board_nodes.h"
#include "RTE_Components.h"
#include CMSIS_device_header
#include "RTE_Device.h"
#include "Driver_CDC200.h"
#include "Driver_Touch_Screen.h"
#include "pinconf.h"
#include "board_config.h"
#include "se_services_port.h"
#include "cmsis_os2.h"
#if RTE_CDC200_PIXEL_FORMAT != 2
#error "LCD node requires RGB565"
#endif
_Static_assert(RTE_PANEL_HACTIVE_TIME == 480 && RTE_PANEL_VACTIVE_LINE == 800,
               "LCD node requires the standard DevKit-E8 panel");
#define CHECK(call) do { int32_t status = (call); if (status != ARM_DRIVER_OK) return status; } while (0)
extern ARM_DRIVER_CDC200 Driver_CDC200;
extern ARM_DRIVER_TOUCH_SCREEN GT911;
static volatile uint32_t scanlines, display_errors;
static uint32_t pending_scanline;
static bool frame_pending;
static void display_callback(uint32_t events)
{
    if (events & ARM_CDC_SCANLINE0_EVENT) ++scanlines;
    display_errors |= events & ARM_CDC_DSI_ERROR_EVENT;
}
static void clean_frame(uint16_t *frame)
{
    SCB_CleanDCache_by_Addr((uint32_t *)frame, LCD_PIXELS * sizeof(uint16_t));
    __DSB();
}
int lcd_init_display(uint16_t *initial_frame)
{
    frame_pending = false;
    uint32_t error = 0;
    run_profile_t profile = {0};
    if (SERVICES_get_run_cfg(se_services_s_handle, &profile, &error) != SERVICES_REQ_SUCCESS || error) return -1;
    profile.memory_blocks |= MRAM_MASK | SRAM0_MASK;
    profile.phy_pwr_gating |= MIPI_PLL_DPHY_MASK | MIPI_TX_DPHY_MASK |
                             MIPI_RX_DPHY_MASK | LDO_PHY_MASK;
    if (SERVICES_set_run_cfg(se_services_s_handle, &profile, &error) != SERVICES_REQ_SUCCESS || error) return -1;
    if (SERVICES_clocks_enable_clock(se_services_s_handle, CLKEN_HFOSCx2, true, &error) != SERVICES_REQ_SUCCESS || error) return -1;
    clean_frame(initial_frame);
    CHECK(Driver_CDC200.Initialize(display_callback));
    CHECK(Driver_CDC200.PowerControl(ARM_POWER_FULL));
    CHECK(Driver_CDC200.Control(CDC200_CONFIGURE_DISPLAY, (uint32_t)initial_frame));
    CHECK(Driver_CDC200.Control(CDC200_SCANLINE0_EVENT, 1U));
    CHECK(Driver_CDC200.Start());
    return 0;
}
int lcd_init(uint16_t *initial_frame)
{
    // Touch owns I2C1's callback for the lifetime of this API. Applications
    // using the MT9M114 on that bus use lcd_init_display instead.
    CHECK(lcd_init_display(initial_frame));
    const uint32_t pad = PADCTRL_READ_ENABLE | PADCTRL_DRIVER_DISABLED_PULL_UP |
                         PADCTRL_OUTPUT_DRIVE_STRENGTH_12MA;
    CHECK(pinconf_set(PORT_7, PIN_2, PINMUX_ALTERNATE_FUNCTION_5, pad));
    CHECK(pinconf_set(PORT_7, PIN_3, PINMUX_ALTERNATE_FUNCTION_5, pad));
    CHECK(pinconf_set(PORT_(BOARD_TOUCH_RESET_GPIO_PORT), BOARD_TOUCH_RESET_GPIO_PIN,
        PINMUX_ALTERNATE_FUNCTION_0, PADCTRL_OUTPUT_DRIVE_STRENGTH_4MA));
    CHECK(pinconf_set(PORT_(BOARD_TOUCH_INT_GPIO_PORT), BOARD_TOUCH_INT_GPIO_PIN,
        PINMUX_ALTERNATE_FUNCTION_0, PADCTRL_READ_ENABLE | PADCTRL_OUTPUT_DRIVE_STRENGTH_4MA));
    CHECK(GT911.Initialize());
    CHECK(GT911.PowerControl(ARM_POWER_FULL));
    return 0;
}
int lcd_wait_for_release(void)
{
    if (display_errors) return ARM_DRIVER_ERROR;
    uint32_t start = osKernelGetTickCount();
    // Two end-of-active-video events ensure the previous buffer is released.
    while (frame_pending && scanlines - pending_scanline < 2U) {
        if (display_errors || osKernelGetTickCount() - start > osKernelGetTickFreq() / 4U)
            return ARM_DRIVER_ERROR_TIMEOUT;
        osDelay(1);
    }
    frame_pending = false;
    return display_errors ? ARM_DRIVER_ERROR : 0;
}
int lcd_submit(uint16_t *frame)
{
    return lcd_submit_region(frame,0,0,480,800);
}
int lcd_submit_region(uint16_t *frame,unsigned x,unsigned y,unsigned width,unsigned height)
{
    if (!frame || x>480 || y>800 || width>480-x || height>800-y || ((uintptr_t)frame & 31U))
        return ARM_DRIVER_ERROR_PARAMETER;
    CHECK(lcd_wait_for_release());
    if(width && height) {
        // Native row stride is 960 bytes, a multiple of the 32-byte cache line.
        unsigned first=(x*2U)&~31U, last=((x+width)*2U+31U)&~31U;
        if(first==0 && last==960) {
            SCB_CleanDCache_by_Addr((uint32_t*)((uint8_t*)frame+y*960U),height*960U);
        } else {
            for(unsigned row=y;row<y+height;++row)
                SCB_CleanDCache_by_Addr((uint32_t*)((uint8_t*)frame+row*960U+first),last-first);
        }
        __DSB();
    }
    CHECK(Driver_CDC200.Control(CDC200_FRAMEBUF_UPDATE_VSYNC, (uint32_t)frame));
    pending_scanline = scanlines;
    frame_pending = true;
    return 0;
}
int lcd_present(uint16_t *frame)
{
    CHECK(lcd_submit(frame));
    return lcd_wait_for_release();
}
int lcd_touch(unsigned *x, unsigned *y, bool *contact)
{
    ARM_TOUCH_STATE state = {0};
    CHECK(GT911.GetState(&state));
    *contact = state.numtouches > 0;
    if (*contact) {
        *x = state.coordinates[0].y;
        *y = 479U - state.coordinates[0].x;
    }
    return 0;
}
