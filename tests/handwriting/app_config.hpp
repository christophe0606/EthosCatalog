// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "datatypes.h"
#include "app_params.h"
inline unsigned osKernelGetTickFreq() { return 1000; }
inline unsigned osKernelGetTickCount() { return 0; }
inline unsigned osKernelGetSysTimerCount() { return 0; }
inline unsigned osKernelGetSysTimerFreq() { return 1000000; }
inline void osDelay(unsigned) {}
