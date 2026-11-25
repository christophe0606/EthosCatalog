// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "cmsis_os2.h"
#include "stream_platform_config.hpp"
#if defined(_MSC_VER) && !defined(__PRETTY_FUNCTION__)
#define __PRETTY_FUNCTION__ __FUNCSIG__
#endif
#define CG_BEFORE_BUFFER alignas(16)
#include "../../projects/dtln_aec/dtln_app_config.hpp"
