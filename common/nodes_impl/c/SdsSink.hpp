// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "NodeSupport.hpp"
#include "SdsEndpoint.hpp"

template<typename IN, int inputSamples>
class SdsSink : public GenericSink<IN, inputSamples> {
public:
    SdsSink(FIFOBase<IN>& in, const SdsSinkParams& params)
        : GenericSink<IN, inputSamples>(in), params_(params) {}

    cg_status init() final {
        return params_.stream && params_.timing ? CG_SUCCESS : CG_INIT_FAILURE;
    }

    int run() final {
        const auto& timing = *params_.timing;
        if (!timing.started) return CG_BUFFER_ERROR;

        // Keep this block queued until SDS has accepted the entire record.
        const IN* in = this->getReadBuffer(0);
        for (;;) {
            lastStatus_ = sdsWrite(params_.stream, timing.timeslot, in, bytes_);
            if (lastStatus_ == bytes_) break;
            const auto status = sds_node_detail::waitToRetry(
                lastStatus_, SDS_NO_SPACE, CG_BUFFER_OVERFLOW, timing.nextReleaseTick);
            if (status != CG_SUCCESS) return status;
        }
        this->getReadBuffer();
        return CG_SUCCESS;
    }

    // The caller binds a new handle while the scheduler is stopped or paused.
    void bindStream(sdsId_t stream) {
        params_.stream = stream;
        lastStatus_ = SDS_OK;
    }

    int32_t lastSdsStatus() const { return lastStatus_; }

private:
    static constexpr int32_t bytes_ = sds_node_detail::recordBytes<IN, inputSamples>();
    SdsSinkParams params_;
    int32_t lastStatus_ = SDS_OK;
};
