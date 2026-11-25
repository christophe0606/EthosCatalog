// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "../../thirdparty/CMSIS-Stream/platform/posix_runtime/stream_event_queue.hpp"

class CMSISEventQueue : public PosixEventQueue {
public:
    using PosixEventQueue::PosixEventQueue;

protected:
    // POSIX owns its native wake mechanism in the base class.
    void* cg_eventEvent = this;
};
