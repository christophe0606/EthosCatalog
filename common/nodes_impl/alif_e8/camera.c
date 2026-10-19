// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
// MT9M114 single-shot adapter; configuration belongs to the board layer.
#include "board_nodes.h"
#include "RTE_Components.h"
#include CMSIS_device_header
#include "RTE_Device.h"
#include "Driver_CPI.h"
#include "cmsis_os2.h"
#if RTE_MT9M114_CAMERA_SENSOR_MIPI_IMAGE_CONFIG != 3
#error "This board camera configuration requires MT9M114 RGB565 VGA"
#endif
extern ARM_DRIVER_CPI Driver_CPI;
volatile uint32_t camera_frames, camera_errors, camera_timeouts;
volatile CameraDiagnostics camera_diagnostics;
static osEventFlagsId_t events;
static int configure_capture(void)
{
    // Ensemble 2.2.1 Stop disables interrupts and resets the CPI controller.
    // Restore its configuration for every exclusive single-shot request.
    int error = Driver_CPI.Control(CPI_CONFIGURE, 0);
    if (!error) error = Driver_CPI.Control(CPI_EVENTS_CONFIGURE,
        ARM_CPI_EVENT_CAMERA_CAPTURE_STOPPED | ARM_CPI_EVENT_MIPI_CSI2_ERROR |
        ARM_CPI_EVENT_ERR_CAMERA_INPUT_FIFO_OVERRUN | ARM_CPI_EVENT_ERR_CAMERA_OUTPUT_FIFO_OVERRUN |
        ARM_CPI_EVENT_ERR_HARDWARE);
    return error;
}
static void camera_callback(uint32_t event)
{
    if (event & ARM_CPI_EVENT_CAMERA_CAPTURE_STOPPED) osEventFlagsSet(events, 1U);
    if (event & (ARM_CPI_EVENT_MIPI_CSI2_ERROR | ARM_CPI_EVENT_ERR_CAMERA_INPUT_FIFO_OVERRUN |
        ARM_CPI_EVENT_ERR_CAMERA_OUTPUT_FIFO_OVERRUN | ARM_CPI_EVENT_ERR_HARDWARE)) {
        ++camera_errors;
        ++camera_diagnostics.error_count[camera_diagnostics.phase];
        camera_diagnostics.error_mask[camera_diagnostics.phase] |= event;
        osEventFlagsSet(events, 2U);
    }
}
int camera_init(void)
{
    events = osEventFlagsNew(NULL);
    if (!events) return -810;
    int error = Driver_CPI.Initialize(camera_callback);
    if (!error) error = Driver_CPI.PowerControl(ARM_POWER_FULL);
    if (!error) error = Driver_CPI.Control(CPI_CAMERA_SENSOR_CONFIGURE, 0);
    if (!error) error = configure_capture();
    camera_diagnostics.phase = CAMERA_IDLE;
    return error;
}
int camera_capture(uint16_t *frame, uint32_t timeout_ticks)
{
    if (!frame || !events) return -811;
    osEventFlagsClear(events, 3U);
    // Remove any dirty CPU lines before camera DMA takes exclusive ownership.
    SCB_CleanInvalidateDCache_by_Addr((uint32_t*)frame, BOARD_CAMERA_BYTES);
    __DSB();
    int error = configure_capture();
    if (error) return error;
    camera_diagnostics.phase = CAMERA_CAPTURING;
    error = Driver_CPI.CaptureFrame((uint8_t*)frame);
    if (error) { camera_diagnostics.phase = CAMERA_IDLE; return error; }
    uint32_t flags = osEventFlagsWait(events, 3U, osFlagsWaitAny, timeout_ticks);
    camera_diagnostics.phase = CAMERA_STOPPING;
    int stop_error = Driver_CPI.Stop();
    camera_diagnostics.phase = CAMERA_IDLE;
    if (flags & osFlagsError) { ++camera_timeouts; return -812; }
    if (flags & 2U) return -813;
    if (stop_error) return stop_error;
    SCB_InvalidateDCache_by_Addr((uint32_t*)frame, BOARD_CAMERA_BYTES);
    __DSB();
    ++camera_frames;
    return 0;
}
