// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "../../../common/nodes_impl/c/NodeSupport.hpp"
#include "../codec_runtime.hpp"

#include <algorithm>
#include <type_traits>

template<typename IN, int codes, typename OUT, int samples>
class EnCodecDecoder : public GenericNode<IN, codes, OUT, samples> {
    static_assert(std::is_same_v<IN, int32_t> && std::is_same_v<OUT, float>);
    static_assert(codes == encodec::blockCodes && samples == encodec::blockSamples);

public:
    using GenericNode<IN, codes, OUT, samples>::GenericNode;

    int run() final {
        const auto error = encodec::decodeBlock(this->getReadBuffer(), this->getWriteBuffer());
        const uint32_t previousDecoder = encodecDemoStats.decoder_ticks_max;
        encodecDemoStats.decoder_ticks_max = std::max(previousDecoder, encodec::encodecLastDecoderTicks);
        const uint32_t blockTicks = encodec::encodecLastEncoderTicks + encodec::encodecLastDecoderTicks;
        const uint32_t previousBlock = encodecDemoStats.codec_ticks_max;
        encodecDemoStats.codec_ticks_max = std::max(previousBlock, blockTicks);
        if (blockTicks > encodecDemoStats.codec_budget_ticks) ++encodecDemoStats.codec_deadline_misses;
        if (error != executorch::runtime::Error::Ok) {
            encodecDemoStats.codec_error = static_cast<int32_t>(error);
            return CG_BUFFER_ERROR;
        }
        if (encodec::encodecLastDecoderTicks > encodecDemoStats.codec_budget_ticks) {
            ++encodecDemoStats.decoder_deadline_misses;
        }
        ++encodecDemoStats.decoder_blocks;
        return CG_SUCCESS;
    }
};
