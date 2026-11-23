// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "app_params.h"
#include "SdsEndpoint.hpp"

#if defined(_MSC_VER)
#define __PRETTY_FUNCTION__ __FUNCSIG__
#endif

#define CG_BEFORE_BUFFER alignas(16)

extern SdsSourceParams replayParams;
extern SdsSinkParams recordParams;
