// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdint.h>
typedef struct { uint32_t count; uint8_t bytes[32]; } UartBlock;
typedef struct { uint32_t generation; char text[164]; } TextLine;
typedef struct { int16_t samples[160]; uint32_t generation,position,total; } AudioBlock;
