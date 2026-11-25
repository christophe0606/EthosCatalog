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
        if (!params_.stream || !params_.timing || !params_.sampleRate ||
            !params_.framesPerRecord || outputSamples % params_.framesPerRecord != 0) {
            return CG_INIT_FAILURE;
        }
        const uint64_t ticks = uint64_t{params_.framesPerRecord} * osKernelGetTickFreq();
        const uint64_t milliseconds = uint64_t{params_.framesPerRecord} * 1000;
        const uint64_t periodTicks = ticks / params_.sampleRate;
        if (!periodTicks || periodTicks > INT32_MAX || ticks % params_.sampleRate ||
            milliseconds % params_.sampleRate || milliseconds / params_.sampleRate > UINT32_MAX) {
            return CG_INIT_FAILURE;
        }
        periodTicks_ = static_cast<uint32_t>(periodTicks);
        periodMs_ = static_cast<uint32_t>(milliseconds / params_.sampleRate);
        *params_.timing = {};
        return CG_SUCCESS;
    }

    int prepareForRunning() final {
        return this->willOverflow() ? CG_SKIP_EXECUTION : CG_SUCCESS;
    }

    int run() final {
        if (this->willOverflow()) return CG_BUFFER_OVERFLOW;

        auto& timing = *params_.timing;
        timing.pacingTimerCounts = 0;
        const uint32_t deadline = timing.started ? timing.nextReleaseTick
                                                 : osKernelGetTickCount() + periodTicks_;
        // Obtain the write address without publishing samples to the FIFO yet.
        OUT* out = this->getWriteBuffer(0);
        uint32_t timeslot = 0;
        for (;;) {
            lastStatus_ = sdsRead(params_.stream, &timeslot, out, bytes_);
            if (lastStatus_ == bytes_) break;
            if (lastStatus_ == SDS_EOS) return CG_STOP_SCHEDULER;
            const auto status = sds_node_detail::waitToRetry(
                lastStatus_, SDS_NO_DATA, CG_BUFFER_UNDERFLOW, deadline);
            if (status != CG_SUCCESS) return status;
        }

        if (timing.started) {
            if (timeslot - timing.timeslot != periodMs_) return CG_BUFFER_ERROR;
            if (sds_node_detail::ticksUntil(deadline) > 0) {
                const auto start = osKernelGetSysTimerCount();
                if (osDelayUntil(deadline) != osOK && sds_node_detail::ticksUntil(deadline) > 0) {
                    return CG_OS_ERROR;
                }
                timing.pacingTimerCounts = osKernelGetSysTimerCount() - start;
            }
        }

        this->getWriteBuffer();
        timing.timeslot = timeslot;
        // Schedule the next release from this one so a delayed graph never emits a burst.
        timing.nextReleaseTick = osKernelGetTickCount() + periodTicks_;
        timing.started = true;
        return CG_SUCCESS;
    }

    // Bind a new recording after initialization, while the scheduler is stopped or paused.
    void bindStream(sdsId_t stream) {
        params_.stream = stream;
        lastStatus_ = SDS_OK;
        *params_.timing = {};
    }

    int32_t lastSdsStatus() const { return lastStatus_; }

private:
    static constexpr int32_t bytes_ = sds_node_detail::recordBytes<OUT, outputSamples>();
    SdsSourceParams params_;
    uint32_t periodTicks_ = 0;
    uint32_t periodMs_ = 0;
    int32_t lastStatus_ = SDS_OK;
};
