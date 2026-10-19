// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdint.h>
#include <stddef.h>
// Non-owning image views. Strides are pixels for RGB565, bytes for byte images.
// sequence/started are opaque frame identity/timestamp values preserved by
// processing. Storage ownership remains with the producer until its next run.
typedef struct { const uint16_t *pixels; uint32_t width,height,stride,sequence,started; } Rgb565Image;
typedef struct { const uint8_t *pixels; uint32_t width,height,stride,sequence,started; } Rgb888Image;
typedef struct { const int8_t *pixels; uint32_t width,height,stride,sequence,started; } GrayInt8Image;
