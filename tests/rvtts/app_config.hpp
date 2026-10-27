// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "datatypes.h"
#include "app_params.h"
extern unsigned test_tick;
inline unsigned osKernelGetTickFreq(){return 1000;}
inline unsigned osKernelGetTickCount(){return test_tick;}
inline unsigned osKernelGetSysTimerFreq(){return 1000000;}
inline unsigned osKernelGetSysTimerCount(){return test_tick*1000;}
inline void osDelay(unsigned ticks){test_tick+=ticks;}
