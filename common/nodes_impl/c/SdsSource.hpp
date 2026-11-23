// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "NodeSupport.hpp"
#include "SdsEndpoint.hpp"

template<typename OUT, int outputSamples>
class SdsSource : public GenericSource<OUT, outputSamples> {
public:
    SdsSource(FIFOBase<OUT>& out, const SdsSourceParams& params)
        : GenericSource<OUT, outputSamples>(out), params_(params) {}

    cg_status init() final {
        return params_.stream && params_.retry.valid() ? CG_SUCCESS : CG_INIT_FAILURE;
    }

    int prepareForRunning() final {
        return this->willOverflow() ? CG_SKIP_EXECUTION : CG_SUCCESS;
    }

    int run() final {
        if (this->willOverflow()) return CG_BUFFER_OVERFLOW;

        // Obtain the write address without publishing samples to the FIFO yet.
        OUT* out = this->getWriteBuffer(0);
        uint32_t timeslot = 0;
        const auto result = sds_node_detail::transferRecord(
            params_.retry, [&] { return sdsRead(params_.stream, &timeslot, out, bytes_); },
            bytes_, SDS_NO_DATA, CG_BUFFER_UNDERFLOW, lastStatus_);
        if (result == CG_SUCCESS) {
            if (params_.beforePublish) {
                const auto status = params_.beforePublish(timeslot);
                if (status != CG_SUCCESS) return status;
            }
            this->getWriteBuffer();
            if (params_.timeslot) *params_.timeslot = timeslot;
            const auto& observer = params_.observer;
            if (observer.observe) observer.observe(observer.context, out, bytes_);
        }
        return result;
    }

    int32_t lastSdsStatus() const { return lastStatus_; }

private:
    static constexpr int32_t bytes_ = sds_node_detail::recordBytes<OUT, outputSamples>();
    const SdsSourceParams params_;
    int32_t lastStatus_ = SDS_OK;
};
