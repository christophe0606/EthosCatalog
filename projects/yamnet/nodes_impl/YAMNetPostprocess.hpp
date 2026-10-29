// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "../../../common/nodes_impl/c/NodeSupport.hpp"
#include "model/class_names.h"

// CPU interpretation of the model's 521 independent sigmoid scores.
// No extra softmax is appropriate for this multilabel classifier.
class YAMNetPostprocess : public StreamNode {
  public:
    static std::array<uint16_t, 3> selectors;
    explicit YAMNetPostprocess(EventQueue *queue) : results_(queue), consumed_(queue) {}

    void subscribe(int port, StreamNode &destination, int destinationPort) final {
        if (port == 0)
            results_.subscribe(destination, destinationPort);
        else if (port == 1)
            consumed_.subscribe(destination, destinationPort);
    }

    cg_status processEvent(int port, Event &&event) final {
        if (port != 0 || event.event_id != selectors[0] ||
            !event.wellFormed<uint64_t, uint32_t>())
            return CG_BUFFER_ERROR;
        const float *scores = nullptr;
        uint32_t sequence = 0;
        event.apply<uint64_t, uint32_t>([&](uint64_t address, uint32_t id) {
            scores = reinterpret_cast<const float *>(uintptr_t(address));
            sequence = id;
        });
        if (!scores)
            return CG_BUFFER_ERROR;

        unsigned top[5]{};
        float values[5] = {-1, -1, -1, -1, -1};
        for (unsigned index = 0; index < 521; ++index) {
            uint32_t bits;
            std::memcpy(&bits, scores + index, sizeof(bits));
            // Bitwise finiteness checks also work with -ffast-math.
            if ((bits & 0x7f800000U) == 0x7f800000U || scores[index] < 0 || scores[index] > 1)
                return CG_BUFFER_ERROR;
            for (unsigned rank = 0; rank < 5; ++rank) {
                if (scores[index] > values[rank]) {
                    for (unsigned next = 4; next > rank; --next) {
                        top[next] = top[next - 1];
                        values[next] = values[next - 1];
                    }
                    top[rank] = index;
                    values[rank] = scores[index];
                    break;
                }
            }
        }

        // Class names are immutable model metadata, so queued labels never expire.
        for (unsigned row = 0; row < 5; ++row) {
            const char *label = yamnet_class_names[top[row]];
            if (!results_.sendAsync(kNormalPriority, selectors[1], uint32_t(row),
                                    uint64_t(reinterpret_cast<uintptr_t>(label)), values[row]))
                return CG_BUFFER_ERROR;
        }
        // Release model output storage only after all score processing is complete.
        if (!consumed_.sendAsync(kNormalPriority, selectors[2], sequence))
            return CG_BUFFER_ERROR;
        return CG_SUCCESS;
    }

  private:
    EventOutput results_, consumed_;
};
