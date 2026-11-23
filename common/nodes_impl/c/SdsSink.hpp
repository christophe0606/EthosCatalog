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
        return params_.stream && params_.timeslot && params_.retry.valid()
            ? CG_SUCCESS : CG_INIT_FAILURE;
    }

    int prepareForRunning() final {
        return this->willUnderflow() ? CG_SKIP_EXECUTION : CG_SUCCESS;
    }

    int run() final {
        if (this->willUnderflow()) return CG_BUFFER_UNDERFLOW;

        // Keep this block queued until SDS has accepted the entire record.
        const IN* in = this->getReadBuffer(0);
        const uint32_t timeslot = params_.timeslot(params_.context);
        const auto result = sds_node_detail::transferRecord(
            params_.retry, [&] { return sdsWrite(params_.stream, timeslot, in, bytes_); },
            bytes_, SDS_NO_SPACE, CG_BUFFER_OVERFLOW, lastStatus_);
        if (result == CG_SUCCESS) {
            const auto& observer = params_.observer;
            if (observer.observe) observer.observe(observer.context, in, bytes_);
            this->getReadBuffer();
        }
        return result;
    }

    int32_t lastSdsStatus() const { return lastStatus_; }

private:
    static constexpr int32_t bytes_ = sds_node_detail::recordBytes<IN, inputSamples>();
    const SdsSinkParams params_;
    int32_t lastStatus_ = SDS_OK;
};
