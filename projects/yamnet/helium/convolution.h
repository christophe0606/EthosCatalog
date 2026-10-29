// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "arm_embedded_module.hpp"
#include <cstdint>
struct YAMNetConvStats {
    uint32_t calls, fallback_calls, vector_calls, ticks;
};
extern YAMNetConvStats yamnet_conv_stats;
int yamnet_install_helium(arm::embedded::EmbeddedModule *module);
