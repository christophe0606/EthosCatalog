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
        if (port != 0 || e.event_id != selectors[0] || !e.wellFormed<uint64_t>())
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
        unsigned top[5]{};
        float val[5] = {-1, -1, -1, -1, -1};
        for (unsigned i = 0; i < 521; ++i) {
            uint32_t bits;
            std::memcpy(&bits, scores_ + i, 4);
            if ((bits & 0x7f800000U) == 0x7f800000U || scores_[i] < 0 || scores_[i] > 1)
                return CG_BUFFER_ERROR;
            for (unsigned j = 0; j < 5; ++j)
                if (scores_[i] > val[j]) {
                    for (unsigned k = 4; k > j; --k) {
                        top[k] = top[k - 1];
                        val[k] = val[k - 1];
                    }
                    top[j] = i;
                    val[j] = scores_[i];
                    break;
                }
        }
        // Fixed-size value events own their class/score; no mutable shared payload.
        for (unsigned i = 0; i < 5; ++i)
            if (!results_.sendAsync(kNormalPriority, selectors[2], uint32_t(i), uint32_t(top[i]),
                                    val[i]))
                return CG_BUFFER_ERROR;
        if (!done_.sendAsync(kNormalPriority, selectors[1], uint32_t(s.inferences)))
            return CG_BUFFER_ERROR;
        return CG_SUCCESS;
    }

  private:
    EventOutput done_, results_;
    YAMNetParams p_;
    float scores_[521]{};
};
