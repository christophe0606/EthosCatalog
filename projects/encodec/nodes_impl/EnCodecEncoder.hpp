// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "../../../common/nodes_impl/c/NodeSupport.hpp"
#include "../codec_runtime.hpp"

#include <algorithm>
#include <type_traits>

template<typename IN, int samples, typename OUT, int codes>
class EnCodecEncoder : public GenericNode<IN, samples, OUT, codes> {
    static_assert(std::is_same_v<IN, float> && std::is_same_v<OUT, int32_t>);
    static_assert(samples == encodec::blockSamples && codes == encodec::blockCodes);

public:
    using GenericNode<IN, samples, OUT, codes>::GenericNode;

    int run() final {
        const auto error = encodec::encodeBlock(this->getReadBuffer(), this->getWriteBuffer());
        const uint32_t previous = encodecDemoStats.encoder_ticks_max;
        encodecDemoStats.encoder_ticks_max = std::max(previous, encodec::encodecLastEncoderTicks);
        if (error != executorch::runtime::Error::Ok) {
            encodecDemoStats.codec_error = static_cast<int32_t>(error);
            return CG_BUFFER_ERROR;
        }
        if (encodec::encodecLastEncoderTicks > encodecDemoStats.codec_budget_ticks) {
            ++encodecDemoStats.encoder_deadline_misses;
        }
        ++encodecDemoStats.encoder_blocks;
        return CG_SUCCESS;
    }
};
