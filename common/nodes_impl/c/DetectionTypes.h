// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdint.h>
// Single-class boxes use continuous edge coordinates. Producers specify ordering.
typedef struct { float x0,y0,x1,y1,score; } ScoredBox;
typedef struct { const ScoredBox *boxes; uint32_t count,sequence,started; } ScoredBoxList;
