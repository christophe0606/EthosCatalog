// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "NodeSupport.hpp"
#include "SdsPictureEndpoint.hpp"

class SdsPictureSink : public StreamNode {
public:
    enum Selector { picture };
    static std::array<uint16_t, 1> selectors;

    explicit SdsPictureSink(const SdsPictureSinkParams& params) : params_(params) {}

    cg_status init() final {
        return params_.stream && params_.pictureBytes && params_.pictureBytes <= INT32_MAX &&
            params_.timeoutTicks <= INT32_MAX ? CG_SUCCESS : CG_INIT_FAILURE;
    }

    cg_status processEvent(int port, Event&& event) final {
        if (port != 0 || event.event_id != selectors[picture] ||
            !event.wellFormed<uint32_t, BufferPtr>()) {
            return CG_BUFFER_ERROR;
        }
        return event.apply<uint32_t, BufferPtr>(&SdsPictureSink::writePicture, *this);
    }

    int32_t lastSdsStatus() const { return lastStatus_; }

private:
    cg_status writePicture(uint32_t timeslot, BufferPtr pixels) {
        if (!pixels) return CG_BUFFER_ERROR;
        bool lockError = false;
        const auto result = pixels.lock_shared(lockError, [&](const RawBuffer& buffer) {
            if (buffer.buf_size != params_.pictureBytes) return CG_BUFFER_ERROR;
            const auto* data = buffer.buffer();
            if (!data) return CG_BUFFER_ERROR;

            const auto deadline = osKernelGetTickCount() + params_.timeoutTicks;
            for (;;) {
                lastStatus_ = sdsWrite(params_.stream, timeslot, data, params_.pictureBytes);
                if (lastStatus_ == static_cast<int32_t>(params_.pictureBytes)) return CG_SUCCESS;
                const auto status = sds_node_detail::waitToRetry(
                    lastStatus_, SDS_NO_SPACE, CG_BUFFER_OVERFLOW, deadline);
                if (status != CG_SUCCESS) return status;
            }
        });
        return lockError ? CG_OS_ERROR : result;
    }

    const SdsPictureSinkParams params_;
    int32_t lastStatus_ = SDS_OK;
};
