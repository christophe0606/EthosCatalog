// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "../../../common/nodes_impl/c/NodeSupport.hpp"
#include "EventQueue.hpp"
#include "embedded_yamnet.h"
#include "main.h"
class YAMNet : public StreamNode {
  public:
    static std::array<uint16_t, 3> selectors;
    YAMNet(EventQueue *q, const YAMNetParams &p) : done_(q), results_(q), p_(p) {}
    cg_status init() final { return p_.hw_.module ? CG_SUCCESS : CG_INIT_FAILURE; }
    void subscribe(int port, StreamNode &dst, int dstPort) final {
        if (port == 0)
            done_.subscribe(dst, dstPort);
        else if (port == 1)
            results_.subscribe(dst, dstPort);
    }
    cg_status processEvent(int port, Event &&e) final {
        if (port == 1) {
            if (!awaiting_postprocess_ || e.event_id != selectors[1] ||
                !e.wellFormed<uint32_t>() || e.get<uint32_t>() != sequence_)
                return CG_BUFFER_ERROR;
            if (!done_.sendAsync(kNormalPriority, selectors[1], sequence_))
                return CG_BUFFER_ERROR;
            awaiting_postprocess_ = false;
            return CG_SUCCESS;
        }
        if (port != 0 || e.event_id != selectors[0] || !e.wellFormed<uint64_t>())
            return CG_BUFFER_ERROR;
        if (awaiting_postprocess_)
            return CG_BUFFER_ERROR;
        auto &s = *p_.hw_.stats;
        uint32_t ticks = 0;
        ethos_benchmark_reset();
        auto *input = reinterpret_cast<float *>(uintptr_t(e.get<uint64_t>()));
        if (!input)
            return CG_BUFFER_ERROR;
        int error = yamnet_forward(p_.hw_.module, input, scores_, &ticks);
        if (error)
            return static_cast<cg_status>(error);
        if (ethos_benchmark.invocations != 14 || !ethos_benchmark.cycles ||
            ethos_benchmark.overflow || ethos_benchmark.cycles != ethos_benchmark.active_cycles)
            return CG_BUFFER_ERROR;
        s.inference_stack_free = osThreadGetStackSpace(osThreadGetId());
        s.forward_ticks = ticks;
        s.npu_cycles = uint32_t(ethos_benchmark.cycles);
        ++s.inferences;
        sequence_ = s.inferences;
        awaiting_postprocess_ = true;
        // Scores remain immutable until the CPU postprocessor acknowledges them.
        if (!results_.sendAsync(kNormalPriority, selectors[2],
                                uint64_t(reinterpret_cast<uintptr_t>(scores_)), sequence_))
            return CG_BUFFER_ERROR;
        return CG_SUCCESS;
    }

  private:
    EventOutput done_, results_;
    YAMNetParams p_;
    float scores_[521]{};
    uint32_t sequence_ = 0;
    bool awaiting_postprocess_ = false;
};
