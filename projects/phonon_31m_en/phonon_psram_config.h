// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "RTE_Device.h"
// DevKit schematic 220-00319 Rev B p.5 wires eight data pins to U5.
// The pack default enables sixteen-bit dual-octal signaling for other boards.
#undef RTE_APS512XXN_PSRAM_DUAL_OCTAL_MODE_ENABLE
#define RTE_APS512XXN_PSRAM_DUAL_OCTAL_MODE_ENABLE 0
