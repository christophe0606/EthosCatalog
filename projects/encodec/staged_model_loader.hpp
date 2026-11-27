// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <executorch/extension/data_loader/buffer_data_loader.h>
#include <executorch/runtime/core/span.h>

namespace encodec {

// Offsets refer to the PTE file including its header. A stage manifest is bound
// to the same image whose CRC is checked before preparing the loader.
struct BackendSegment {
    std::size_t offset;
    std::size_t size;
    unsigned bank;
};

struct StageBank {
    uint8_t* storage;
    std::size_t capacity;
};

class StagedModelLoader final : public executorch::runtime::DataLoader {
public:
    using Banks = std::array<StageBank, 2>;

    StagedModelLoader(const uint8_t* model, std::size_t modelSize,
                      const uint8_t* program, std::size_t programSize);

    // Called once before loading any methods. Storage outlives the program and
    // is 32-byte aligned in NPU-visible SRAM.
    executorch::runtime::Error stage(executorch::runtime::Span<const BackendSegment> segments, const Banks& banks);

    executorch::runtime::Result<executorch::runtime::FreeableBuffer>
    load(std::size_t offset, std::size_t size, const SegmentInfo& info) const override;

    executorch::runtime::Error load_into(std::size_t offset, std::size_t size,
                                         const SegmentInfo& info, void* buffer) const override;

    executorch::runtime::Result<std::size_t> size() const override;

private:
    const uint8_t* const model_;
    const std::size_t modelSize_;
    const uint8_t* const program_;
    const std::size_t programSize_;
    executorch::extension::BufferDataLoader ospi_;
    static constexpr std::size_t maxSegments = 9;
    std::array<BackendSegment, maxSegments> segments_{};
    std::array<const uint8_t*, maxSegments> staged_{};
};

}
