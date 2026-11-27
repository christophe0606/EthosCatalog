// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <executorch/runtime/core/error.h>

namespace arm::embedded {
class EmbeddedModule;
}

namespace encodec {

executorch::runtime::Error installCpuKernels(arm::embedded::EmbeddedModule& module);

}
