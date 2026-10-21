// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "embedded_gain.h"
#include "embedded_runtime.hpp"

extern "C" int embedded_gain_forward(void *module, float *input, float *output, size_t count)
{
    return arm::embedded::gain_forward(static_cast<arm::embedded::EmbeddedModule*>(module), input, output, count);
}
