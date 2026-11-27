// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "staged_model_loader.hpp"

#include "RTE_Components.h"
#include CMSIS_device_header
#include "app_mem_regions.h"
#include "ethosu_driver.h"

#include <cstring>

namespace {
constexpr std::size_t alignment = 32;
constexpr uint64_t sramBegin = APP_SRAM0_BASE;
// SRAM1 follows SRAM0 through the bus alias verified on DevKit-E8.
constexpr uint64_t sramEnd = sramBegin + APP_SRAM0_SIZE + APP_SRAM1_SIZE;
}

extern "C" unsigned int ethosu_config_select(uint64_t address, int index)
{
    // Vela still places its constants in region 0 and its arena in region 1.
    // Select AXI_SRAM for staged constants and commands and AXI_EXT for OSPI.
    if (index > 0) return index == 1 ? 0U : 1U;
    if (address >= sramBegin && address < sramEnd) return 1U;
    return index == -1 ? 2U : 3U;
}

namespace encodec {
using executorch::runtime::Error;
using executorch::runtime::FreeableBuffer;
using executorch::runtime::Result;

StagedModelLoader::StagedModelLoader(const uint8_t* model, std::size_t modelSize,
                                     const uint8_t* program, std::size_t programSize)
    : model_(model), modelSize_(modelSize), program_(program), programSize_(programSize),
      ospi_(model, modelSize) {}

Error StagedModelLoader::stage(executorch::runtime::Span<const BackendSegment> segments, const Banks& banks)
{
    if (staged_.front()) return Error::InvalidState;
    if (segments.empty() || segments.size() > staged_.size()) return Error::InvalidArgument;
    for (const auto& bank : banks) {
        if (!bank.storage || reinterpret_cast<uintptr_t>(bank.storage) % alignment) {
            return Error::InvalidArgument;
        }
    }
    std::array<uint8_t*, maxSegments> destinations;
    std::array<std::size_t, Banks{}.size()> used{};
    for (std::size_t i = 0; i < segments.size(); ++i) {
        const auto [offset, bytes, index] = segments[i];
        if (!bytes || offset > modelSize_ || bytes > modelSize_ - offset || index >= banks.size()) {
            return Error::InvalidProgram;
        }
        const auto& bank = banks[index];
        auto& end = used[index];
        const auto padding = (alignment - end % alignment) % alignment;
        if (padding > bank.capacity - end || bytes > bank.capacity - end - padding) {
            return Error::MemoryAllocationFailed;
        }
        destinations[i] = bank.storage + end + padding;
        end += padding + bytes;
    }
    for (std::size_t i = 0; i < segments.size(); ++i) {
        auto* destination = destinations[i];
        std::memcpy(destination, model_ + segments[i].offset, segments[i].size);
        segments_[i] = segments[i];
        staged_[i] = destination;
    }
    // Publish written cache lines before the NPU may read the staged data.
    for (std::size_t i = 0; i < banks.size(); ++i) {
        SCB_CleanDCache_by_Addr(reinterpret_cast<uint32_t*>(banks[i].storage), static_cast<int32_t>(used[i]));
    }
    __DSB();
    return Error::Ok;
}

Result<FreeableBuffer> StagedModelLoader::load(std::size_t offset, std::size_t bytes,
                                             const SegmentInfo& info) const
{
    if (info.segment_type == SegmentInfo::Type::Program) {
        if (offset > programSize_ || bytes > programSize_ - offset) return Error::InvalidProgram;
        return FreeableBuffer(program_ + offset, bytes, nullptr);
    }
    if (info.segment_type == SegmentInfo::Type::Backend) {
        for (std::size_t i = 0; i < segments_.size(); ++i) {
            if (staged_[i] && offset == segments_[i].offset && bytes == segments_[i].size) {
                return FreeableBuffer(staged_[i], bytes, nullptr);
            }
        }
    }
    return ospi_.load(offset, bytes, info);
}

Error StagedModelLoader::load_into(std::size_t offset, std::size_t bytes,
                                   const SegmentInfo& info, void* buffer) const
{
    return ospi_.load_into(offset, bytes, info, buffer);
}

Result<std::size_t> StagedModelLoader::size() const
{
    return ospi_.size();
}

}
