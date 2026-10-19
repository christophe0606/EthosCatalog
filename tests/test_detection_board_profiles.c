// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "RTE_Device.h"
#ifdef OBJECT_DETECTION_RGB565
#if RTE_CPI_ISP_PORT != 0 || RTE_MT9M114_CAMERA_SENSOR_MIPI_IMAGE_CONFIG != 3 || RTE_MT9M114_CAMERA_SENSOR_FRAME_WIDTH != 640 || RTE_MT9M114_CAMERA_SENSOR_FRAME_HEIGHT != 480
#error "Detection camera profile changed"
#endif
#else
#if RTE_CPI_ISP_PORT != 1 || RTE_MT9M114_CAMERA_SENSOR_MIPI_IMAGE_CONFIG != 1 || RTE_MT9M114_CAMERA_SENSOR_FRAME_WIDTH != 1280 || RTE_MT9M114_CAMERA_SENSOR_FRAME_HEIGHT != 720
#error "Existing projects' camera profile changed"
#endif
#endif
int detection_board_profile_check;
