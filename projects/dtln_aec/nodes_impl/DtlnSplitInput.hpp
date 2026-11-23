// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "../../../common/nodes_impl/c/NodeSupport.hpp"

template<typename IN, int ni, typename MIC, int nm, typename REF, int nr>
class DtlnSplitInput : public GenericNode12<IN, ni, MIC, nm, REF, nr> {
public:
    DtlnSplitInput(FIFOBase<IN>& input, FIFOBase<MIC>& mic, FIFOBase<REF>& ref)
        : GenericNode12<IN, ni, MIC, nm, REF, nr>(input, mic, ref) {
        static_assert(ni == 2 * nm && nm == nr);
    }

    int run() final {
        const auto* input = this->getReadBuffer();
        auto* mic = this->getWriteBuffer1();
        auto* ref = this->getWriteBuffer2();
        for (int i = 0; i < nm; ++i) {
            mic[i] = input[2 * i];
            ref[i] = input[2 * i + 1];
        }
        return CG_SUCCESS;
    }
};
