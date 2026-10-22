// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <atomic>
#include "datatypes.h"
#include "handwriting_engine.hpp"
namespace handwriting {
// CMSIS Stream's fixed value variant carries local addresses as uint64_t.
// SendToNetwork retains its immutable request until the completion ACK.
// Network publishes its retained Result BEFORE ACK at the same queue priority;
// FIFO dispatch draws/releases that result before another request can overwrite
// it. This protocol is application-local, never an external pointer interface.
inline bool current(const SentenceSnapshot& request) {
    return request.current && static_cast<std::atomic<uint32_t>*>(request.current)->load(std::memory_order_acquire)==request.generation;
}
struct Result {
    SentenceSnapshot request;
    Stroke point;
    uint32_t kind; // 0 point, 1 clear, 2 completion, 3 step bound
    uint32_t character;
    bool word_boundary;
};
}
