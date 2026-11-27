// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "codec_runtime.hpp"

#include "arm_embedded_module.hpp"
#include "cmsis_os2.h"
#include "kernels/cpu_kernels.hpp"
#include "main.h"
#include "staging_manifest.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <type_traits>
#include <executorch/runtime/platform/runtime.h>

extern "C" int e8_flash_set_read_clock(uint32_t hz);

namespace encodec {
using namespace executorch::aten;
using namespace executorch::runtime;

namespace {
constexpr unsigned delegatesPerMethod = 16;
unsigned currentMethod = methodCount;
}

struct DelegateStats {
    uint32_t weightsAddress;
    uint32_t weightsBytes;
    uint64_t npuCycles;
};

extern "C" {
MethodStats encodecMethodStats[methodCount];
DelegateStats encodecDelegateStats[methodCount][delegatesPerMethod];
uint32_t encodecModelCrc;
uint32_t encodecLastBlockTicks;
uint32_t encodecLastEncoderTicks;
uint32_t encodecLastDecoderTicks;
#ifdef ENCODEC_CPU_PROFILING
uint32_t encodecProfileCopyBytes;
uint32_t encodecProfileOspiCopyTicks;
uint32_t encodecProfileSramCopyTicks;
#endif

void ethosu_delegate_profile(unsigned invocation, uint32_t weightsAddress, uint32_t weightsSize,
                             uint64_t cycles)
{
    if (currentMethod < methodCount && invocation && invocation <= delegatesPerMethod) {
        encodecDelegateStats[currentMethod][invocation - 1] = {weightsAddress, weightsSize, cycles};
    }
}
}

namespace {

constexpr unsigned channels = 512;
constexpr unsigned gates = 4 * channels;
constexpr unsigned stateSize = 2 * channels;
constexpr std::size_t methodBytes = 0x60000;

const std::array<std::string, methodCount> methods{
    "encode_front", "encode_hidden0", "encode_project1", "encode_hidden1", "encode_back",
    "decode_front", "decode_hidden0", "decode_project1", "decode_hidden1", "decode_back"};

template<unsigned Front, unsigned Back>
struct DirectionState {
    alignas(32) float front[Front];
    alignas(32) float hidden0[stateSize];
    alignas(32) float hidden1[stateSize];
    alignas(32) float back[Back];
};

struct Scratch {
    DirectionState<3974, 3072> encoder;
    alignas(32) float projections[framesPerBlock * gates];
    alignas(32) int32_t codes[blockCodes];
};

// Keep encoder state and projections in DTCM. The larger USB firmware also
// needs DTCM for its non-secure buffers and runtime stacks.
alignas(32) Scratch scratch;
alignas(32) float featureRows[framesPerBlock * channels] __attribute__((section(".bss.ai_pool")));
alignas(32) float recurrentRows[framesPerBlock * channels] __attribute__((section(".bss.ai_pool")));
alignas(32) DirectionState<768, 2112> decoderState __attribute__((section(".bss.encodec_staging")));
alignas(32) uint8_t methodPool[methodBytes];
alignas(32) uint8_t tempPool[tempBytes] __attribute__((section(".bss.ai_pool")));
alignas(32) uint8_t arena[activationBytes] __attribute__((section(".bss.ai_pool")));
alignas(32) uint8_t stagedSram0[stagedBytes[0]] __attribute__((section(".bss.ai_pool")));
alignas(32) uint8_t stagedSram1[stagedBytes[1]] __attribute__((section(".bss.encodec_staging")));
// Static TensorImpl sizes and dimension order remain views into this buffer.
alignas(32) uint8_t programMetadata[programBytes];

Span<uint8_t> arenaSpan(arena, sizeof(arena));
HierarchicalAllocator planned(Span<Span<uint8_t>>(&arenaSpan, 1));
std::unique_ptr<arm::embedded::EmbeddedModule> module;
bool ready;
bool encoderResetPending = true;
bool decoderResetPending = true;

template<typename T>
constexpr ScalarType scalar()
{
    if constexpr (std::is_same_v<T, float>) return ScalarType::Float;
    if constexpr (std::is_same_v<T, int32_t>) return ScalarType::Int;
    static_assert(std::is_same_v<T, float> || std::is_same_v<T, int32_t> || std::is_same_v<T, bool>);
    return ScalarType::Bool;
}

template<typename T, std::size_t Rank>
class View {
public:
    View(T* data, const std::array<SizesType, Rank>& shape)
        : shape_(shape), impl_(scalar<T>(), Rank, shape_.data(), data, order_.data()) {}

    Tensor tensor() { return Tensor(&impl_); }

private:
    std::array<DimOrderType, Rank> order_ = [] {
        std::array<DimOrderType, Rank> order{};
        for (std::size_t i = 0; i < Rank; ++i) order[i] = static_cast<DimOrderType>(i);
        return order;
    }();
    std::array<SizesType, Rank> shape_;
    TensorImpl impl_;
};

template<typename T>
Error copyOutput(const EValue& value, T* output, std::size_t elements)
{
    if (!value.isTensor()) return Error::InvalidType;
    const auto tensor = value.toTensor();
    if (tensor.scalar_type() != scalar<T>() || tensor.nbytes() != elements * sizeof(T)) {
        return Error::InvalidArgument;
    }
    std::memcpy(output, tensor.const_data_ptr<T>(), tensor.nbytes());
    return Error::Ok;
}

Result<std::vector<EValue>> invoke(unsigned index, const std::vector<EValue>& inputs)
{
    ethos_benchmark_reset();
    currentMethod = index;
    const uint32_t start = osKernelGetSysTimerCount();
    auto result = module->execute(methods[index], inputs);
    const uint32_t elapsed = osKernelGetSysTimerCount() - start;
    currentMethod = methodCount;
    auto& stats = encodecMethodStats[index];
    ++stats.calls;
    stats.lastTicks = elapsed;
    stats.maxTicks = std::max(stats.maxTicks, elapsed);
    stats.npuCalls = ethos_benchmark.invocations;
    stats.npuOverflow = ethos_benchmark.overflow;
    stats.weightsAddress = ethos_benchmark.weights_address;
    stats.npuCycles = ethos_benchmark.cycles;
    return result;
}

Error runHidden(unsigned method, float (&state)[stateSize], bool reset)
{
    View<float, 1> storedState(state, {stateSize});
    for (unsigned frame = 0; frame < framesPerBlock; ++frame) {
        bool first = reset && frame == 0;
        View<bool, 1> firstView(&first, {1});
        View<float, 2> projection(scratch.projections + frame * gates, {1, gates});
        auto result = invoke(method, {projection.tensor(), storedState.tensor(), firstView.tensor()});
        if (!result.ok()) return result.error();
        if (result->size() != 2) return Error::InvalidProgram;
        ET_CHECK_OK_OR_RETURN_ERROR(copyOutput((*result)[0], recurrentRows + frame * channels, channels));
        ET_CHECK_OK_OR_RETURN_ERROR(copyOutput((*result)[1], state, stateSize));
    }
    return Error::Ok;
}

template<typename In, typename Out, unsigned Front, unsigned Back>
Error processDirection(unsigned base, In* input, Out* output,
                       DirectionState<Front, Back>& state, bool reset)
{
    constexpr std::array<SizesType, 3> audioShape{1, 1, blockSamples};
    constexpr std::array<SizesType, 3> codeShape{1, codebooks, framesPerBlock};
    constexpr std::array<SizesType, 3> featuresShape{1, framesPerBlock, channels};
    View<In, 3> inputView(input, std::is_same_v<In, float> ? audioShape : codeShape);
    View<float, 1> frontState(state.front, {Front});
    View<bool, 1> resetView(&reset, {1});
    {
        auto result = invoke(base, {inputView.tensor(), frontState.tensor(), resetView.tensor()});
        if (!result.ok()) return result.error();
        if (result->size() != 3) return Error::InvalidProgram;
        ET_CHECK_OK_OR_RETURN_ERROR(copyOutput((*result)[0], featureRows, framesPerBlock * channels));
        ET_CHECK_OK_OR_RETURN_ERROR(copyOutput((*result)[1], scratch.projections, framesPerBlock * gates));
        ET_CHECK_OK_OR_RETURN_ERROR(copyOutput((*result)[2], state.front, Front));
    }

    View<float, 3> recurrent(recurrentRows, featuresShape);
    ET_CHECK_OK_OR_RETURN_ERROR(runHidden(base + 1, state.hidden0, reset));
    {
        auto result = invoke(base + 2, {recurrent.tensor()});
        if (!result.ok()) return result.error();
        if (result->size() != 1) return Error::InvalidProgram;
        ET_CHECK_OK_OR_RETURN_ERROR(copyOutput((*result)[0], scratch.projections, framesPerBlock * gates));
    }
    ET_CHECK_OK_OR_RETURN_ERROR(runHidden(base + 3, state.hidden1, reset));

    View<float, 3> features(featureRows, featuresShape);
    View<float, 1> backState(state.back, {Back});
    auto result = invoke(base + 4, {features.tensor(), recurrent.tensor(), backState.tensor(), resetView.tensor()});
    if (!result.ok()) return result.error();
    if (result->size() != 2) return Error::InvalidProgram;
    const unsigned outputSamples = std::is_same_v<Out, float> ? blockSamples : blockCodes;
    ET_CHECK_OK_OR_RETURN_ERROR(copyOutput((*result)[0], output, outputSamples));
    return copyOutput((*result)[1], state.back, Back);
}

uint32_t pteCrc()
{
    uint32_t crc = 0xffffffffU;
    for (std::size_t i = 0; i < model_pte_size; ++i) {
        crc ^= model_pte[i];
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
        }
    }
    return ~crc;
}

}

const char* methodName(unsigned index)
{
    return index < methods.size() ? methods[index].c_str() : nullptr;
}

#ifdef ENCODEC_CPU_PROFILING
unsigned profileMethod()
{
    return currentMethod;
}

Error profileModelCopies()
{
    if (!ready) return Error::InvalidState;
    constexpr std::size_t sampleBytes = std::min({std::size_t{256 * 1024}, tempBytes, stagedBytes[0]});
    if (!sampleBytes || model_pte_size < sampleBytes) return Error::InvalidProgram;
    const uint8_t* const ospiSample = model_pte + model_pte_size - sampleBytes;
    uint32_t start = osKernelGetSysTimerCount();
    std::memcpy(tempPool, ospiSample, sampleBytes);
    encodecProfileOspiCopyTicks = osKernelGetSysTimerCount() - start;
    if (std::memcmp(tempPool, ospiSample, sampleBytes)) return Error::AccessFailed;
    start = osKernelGetSysTimerCount();
    std::memcpy(tempPool, stagedSram0, sampleBytes);
    encodecProfileSramCopyTicks = osKernelGetSysTimerCount() - start;
    if (std::memcmp(tempPool, stagedSram0, sampleBytes)) return Error::AccessFailed;
    encodecProfileCopyBytes = sampleBytes;
    return Error::Ok;
}
#endif

Error initializeCodec()
{
    if (ready) return Error::Ok;
    if (e8_flash_set_read_clock(100000000U)) return Error::AccessFailed;
    if (model_pte_size != MODEL_PTE_SIZE_BYTES) return Error::InvalidProgram;
    encodecModelCrc = pteCrc();
    if (encodecModelCrc != MODEL_PTE_CRC32) return Error::InvalidProgram;

    runtime_init();
    std::memcpy(programMetadata, model_pte, sizeof(programMetadata));
    auto loader = std::make_unique<StagedModelLoader>(model_pte, model_pte_size,
        programMetadata, sizeof(programMetadata));
    const StagedModelLoader::Banks banks{{
        {stagedSram0, sizeof(stagedSram0)}, {stagedSram1, sizeof(stagedSram1)}}};
    ET_CHECK_OK_OR_RETURN_ERROR(loader->stage({stagedSegments.data(), stagedSegments.size()}, banks));
    auto candidate = std::make_unique<arm::embedded::EmbeddedModule>(model_pte, model_pte_size,
        std::move(loader), std::make_unique<MemoryAllocator>(sizeof(methodPool), methodPool),
        std::make_unique<MemoryAllocator>(sizeof(tempPool), tempPool));
    ET_CHECK_OK_OR_RETURN_ERROR(installCpuKernels(*candidate));
    for (const auto& name : methods) {
        auto meta = candidate->method_meta(name);
        if (!meta.ok()) return meta.error();
        if (meta->num_memory_planned_buffers() != 1) return Error::InvalidProgram;
        auto size = meta->memory_planned_buffer_size(0);
        if (!size.ok()) return size.error();
        if (static_cast<std::size_t>(*size) > sizeof(arena)) return Error::MemoryAllocationFailed;
        ET_CHECK_OK_OR_RETURN_ERROR(candidate->load_method(name, &planned));
    }
    module = std::move(candidate);
    ready = true;
    resetCodec();
    return Error::Ok;
}

void resetEncoder()
{
    std::memset(&scratch.encoder, 0, sizeof(scratch.encoder));
    encoderResetPending = true;
}

void resetDecoder()
{
    std::memset(&decoderState, 0, sizeof(decoderState));
    decoderResetPending = true;
}

void resetCodec()
{
    resetEncoder();
    resetDecoder();
}

Error encodeBlock(float* input, int32_t* codes)
{
    if (!ready) return Error::InvalidState;
    if (!input || !codes) return Error::InvalidArgument;
    const uint32_t start = osKernelGetSysTimerCount();
    const auto error = processDirection(0, input, codes, scratch.encoder, encoderResetPending);
    encodecLastEncoderTicks = osKernelGetSysTimerCount() - start;
    if (error == Error::Ok) encoderResetPending = false;
    else resetEncoder();
    return error;
}

Error decodeBlock(int32_t* codes, float* output)
{
    if (!ready) return Error::InvalidState;
    if (!codes || !output) return Error::InvalidArgument;
    const uint32_t start = osKernelGetSysTimerCount();
    const auto error = processDirection(5, codes, output, decoderState, decoderResetPending);
    encodecLastDecoderTicks = osKernelGetSysTimerCount() - start;
    if (error == Error::Ok) decoderResetPending = false;
    else resetDecoder();
    return error;
}

Error processBlock(float* input, float* output)
{
    if (!ready) return Error::InvalidState;
    if (!input || !output) return Error::InvalidArgument;
    const uint32_t start = osKernelGetSysTimerCount();
    auto error = encodeBlock(input, scratch.codes);
    if (error == Error::Ok) error = decodeBlock(scratch.codes, output);
    encodecLastBlockTicks = osKernelGetSysTimerCount() - start;
    if (error != Error::Ok) resetCodec();
    return error;
}

}
