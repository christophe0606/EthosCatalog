// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "cg_enums.h"
#include "app_config.hpp"
#include "StreamNode.hpp"
#include "GenericNodes.hpp"
#include "nodes.h"
#include <array>
#include <cstring>
#include <type_traits>
using namespace arm_cmsis_stream;
template<class Block> constexpr size_t block_samples = std::extent_v<decltype(Block::samples)>;
