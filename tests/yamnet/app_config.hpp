// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "../stream/app_config.hpp"
inline void* osThreadGetId(){return nullptr;}
inline uint32_t osThreadGetStackSpace(void*){return 4096;}
