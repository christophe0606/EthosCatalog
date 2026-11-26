// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "NodeSupport.hpp"
#include "EventQueue.hpp"
#include "SdsPictureEndpoint.hpp"

class SdsPictureSource : public StreamNode {
public:
    enum Selector { picture, pictureEnd, pictureError, pictureDropped };
    static std::array<uint16_t, 4> selectors;

    SdsPictureSource(EventQueue* queue, const SdsPictureSourceParams& params)
        : queue_(queue), pictures_(queue), status_(queue), params_(params) {}

    cg_status init() final {
        if (!queue_ || !params_.stream || !params_.pool || !params_.pictureBytes ||
            params_.pictureBytes > INT32_MAX || params_.timeoutTicks > INT32_MAX ||
            params_.framesPerSecond > 1000 ||
            osMemoryPoolGetBlockSize(params_.pool) < params_.poolBlockBytes(params_.pictureBytes)) {
            return CG_INIT_FAILURE;
        }
        if (skipOnOverflow() && !discardPixels_) {
            if (osMemoryPoolGetCapacity(params_.pool) < 2) return CG_INIT_FAILURE;
            discardPixels_ = allocatePixels();
            if (!discardPixels_) return CG_MEMORY_ALLOCATION_FAILURE;
        }
        hasTimeslot_ = false;
        lastStatus_ = SDS_OK;
        return CG_SUCCESS;
    }

    void subscribe(int port, StreamNode& dst, int dstPort) final {
        if (port == 0) pictures_.subscribe(dst, dstPort);
        if (port == 1) status_.subscribe(dst, dstPort);
    }

    cg_status processEvent(int port, Event&& event) final {
        lastStatus_ = SDS_OK;
        if (port != 0 || event.event_id != kDo) return reportError(CG_BUFFER_ERROR);

        auto pixels = allocatePixels();
        const bool poolExhausted = !pixels;
        if (poolExhausted && !discardPixels_) return reportError(CG_MEMORY_ALLOCATION_FAILURE);
        auto* data = poolExhausted ? discardPixels_.get() : pixels.get();

        const auto deadline = osKernelGetTickCount() + params_.timeoutTicks;
        uint32_t timeslot = 0;
        for (;;) {
            lastStatus_ = sdsRead(params_.stream, &timeslot, data, params_.pictureBytes);
            if (lastStatus_ == static_cast<int32_t>(params_.pictureBytes)) break;
            if (lastStatus_ == SDS_EOS) {
                if (status_.sendAsync(kNormalPriority, selectors[pictureEnd])) return CG_SUCCESS;
                return reportError(CG_EVENT_QUEUE_FULL);
            }
            const auto status = sds_node_detail::waitToRetry(
                lastStatus_, SDS_NO_DATA, CG_BUFFER_UNDERFLOW, deadline);
            if (status != CG_SUCCESS) return reportError(status);
        }

        const auto timing = checkTimeslot(timeslot);
        if (timing != CG_SUCCESS) {
            return reportError(timing, timing == CG_BUFFER_OVERFLOW && skipOnOverflow());
        }
        if (poolExhausted) return reportError(CG_MEMORY_ALLOCATION_FAILURE, true);

        auto buffer = BufferPtr::create_with(params_.pictureBytes, std::move(pixels));
        if (!buffer) return reportError(CG_MEMORY_ALLOCATION_FAILURE);
        Event output(selectors[picture], kNormalPriority, timeslot, std::move(buffer));
        if (!output) return reportError(CG_MEMORY_ALLOCATION_FAILURE);
        if (!pictures_.sendAsync(std::move(output))) {
            return reportError(CG_EVENT_QUEUE_FULL, skipOnOverflow());
        }
        return CG_SUCCESS;
    }

    int32_t lastSdsStatus() const { return lastStatus_; }

private:
    bool skipOnOverflow() const { return params_.overflowPolicy == SdsPictureOverflowPolicy::Skip; }

    UniquePtr<std::byte> allocatePixels() const {
        auto* block = static_cast<std::byte*>(osMemoryPoolAlloc(params_.pool, 0));
        if (!block) return {};
        // The deleter gets only the pixel address and may run after the node is destroyed.
        std::memcpy(block, &params_.pool, sizeof(params_.pool));
        return UniquePtr<std::byte>(block + sizeof(params_.pool), releaseBlock);
    }

    cg_status checkTimeslot(uint32_t timeslot) {
        if (hasTimeslot_) {
            // Unsigned subtraction accepts a forward step across the 32-bit wrap.
            const uint32_t delta = timeslot - lastTimeslot_;
            if (!delta || delta > INT32_MAX) return CG_BUFFER_ERROR;

            const auto fps = params_.framesPerSecond;
            if (fps && delta < 1000 / fps) return CG_BUFFER_ERROR;
            if (fps && delta > (1000 + fps - 1) / fps) {
                if (skipOnOverflow()) lastTimeslot_ = timeslot;
                return CG_BUFFER_OVERFLOW;
            }
        }
        lastTimeslot_ = timeslot;
        hasTimeslot_ = true;
        return CG_SUCCESS;
    }

    cg_status reportError(cg_status error, bool skipped = false) {
        const auto selector = selectors[skipped ? pictureDropped : pictureError];
        const auto status = static_cast<int32_t>(error);
        // Direct delivery works when the picture queue is full and before a fatal return pauses it.
        status_.sendSync(kHighPriority, selector, status);
        EventQueue::callSyncHandler(nodeID(), Event(selector, kHighPriority, status));
        return skipped ? CG_SUCCESS : error;
    }

    static void releaseBlock(void* data) {
        auto* block = static_cast<std::byte*>(data) - sizeof(osMemoryPoolId_t);
        osMemoryPoolId_t pool;
        std::memcpy(&pool, block, sizeof(pool));
        osMemoryPoolFree(pool, block);
    }

    EventQueue* const queue_;
    EventOutput pictures_;
    EventOutput status_;
    const SdsPictureSourceParams params_;
    UniquePtr<std::byte> discardPixels_;
    uint32_t lastTimeslot_ = 0;
    bool hasTimeslot_ = false;
    int32_t lastStatus_ = SDS_OK;
};
