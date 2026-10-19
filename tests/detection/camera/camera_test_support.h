// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdint.h>
#include <stddef.h>
#define BOARD_CAMERA_BYTES (640U*480U*2U)
#define CMSIS_device_header "camera_test_support.h"
#define RTE_MT9M114_CAMERA_SENSOR_MIPI_IMAGE_CONFIG 3
#define ARM_POWER_FULL 1
#define CPI_CAMERA_SENSOR_CONFIGURE 1
#define CPI_CONFIGURE 2
#define CPI_EVENTS_CONFIGURE 3
#define ARM_CPI_EVENT_CAMERA_CAPTURE_STOPPED 1U
#define ARM_CPI_EVENT_MIPI_CSI2_ERROR 2U
#define ARM_CPI_EVENT_ERR_CAMERA_INPUT_FIFO_OVERRUN 4U
#define ARM_CPI_EVENT_ERR_CAMERA_OUTPUT_FIFO_OVERRUN 8U
#define ARM_CPI_EVENT_ERR_HARDWARE 16U
#define osFlagsWaitAny 0U
#define osFlagsError 0x80000000U
enum { CAMERA_INITIALIZING, CAMERA_IDLE, CAMERA_CAPTURING, CAMERA_STOPPING };
typedef struct { uint32_t phase, error_count[4], error_mask[4]; } CameraDiagnostics;
extern volatile CameraDiagnostics camera_diagnostics;
typedef void *osEventFlagsId_t;
typedef struct {
    int (*Initialize)(void (*callback)(uint32_t));
    int (*PowerControl)(int);
    int (*Control)(uint32_t,uint32_t);
    int (*CaptureFrame)(uint8_t*);
    int (*Stop)(void);
} ARM_DRIVER_CPI;
osEventFlagsId_t osEventFlagsNew(const void*);
uint32_t osEventFlagsSet(osEventFlagsId_t,uint32_t);
uint32_t osEventFlagsClear(osEventFlagsId_t,uint32_t);
uint32_t osEventFlagsWait(osEventFlagsId_t,uint32_t,uint32_t,uint32_t);
void SCB_CleanInvalidateDCache_by_Addr(uint32_t*,uint32_t);
void SCB_InvalidateDCache_by_Addr(uint32_t*,uint32_t);
void __DSB(void);
