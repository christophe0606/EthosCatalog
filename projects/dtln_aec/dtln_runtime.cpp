// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "dtln_runtime.h"
#include "embedded_runtime.hpp"
#include "model_pte.h"
#include "cmsis_os2.h"
#include <array>
#include <cstring>
#if defined(DTLN_USE_FAST_CPU_KERNELS)
#include "kernels/normalization.hpp"
#include "kernels/quantization.hpp"
#endif

extern "C" int e8_flash_set_read_clock(uint32_t hz);
extern "C" uint32_t GetSystemAXIClock(void);

#ifndef DTLN_MODEL_IN_SRAM
#define DTLN_MODEL_IN_SRAM 0
#endif
#if DTLN_MODEL_IN_SRAM && (NPU_MEM_ATTR_2 != 0x20 || NPU_MEM_ATTR_3 != 0x20)
#error The SRAM model cache requires NPU_MEM_ATTR_2 and NPU_MEM_ATTR_3 to be 0x20
#endif

namespace {
alignas(32) uint8_t method_pool[APP_METHOD_POOL_SIZE] __attribute__((section(APP_POOL_SECTION)));
alignas(32) uint8_t temp_pool[APP_TEMP_POOL_SIZE] __attribute__((section(APP_POOL_SECTION)));
alignas(32) float input_signal[512] __attribute__((section(APP_POOL_SECTION)));
alignas(32) float input_reference[512] __attribute__((section(APP_POOL_SECTION)));
alignas(32) float input_state[DTLN_STATE_VALUES] __attribute__((section(APP_POOL_SECTION)));
#if DTLN_MODEL_IN_SRAM
alignas(32) uint8_t pte_cache[MODEL_PTE_SIZE_BYTES] __attribute__((section(APP_POOL_SECTION)));
#endif
arm::embedded::EmbeddedModule *module;
const std::string method_names[]{"stage1", "stage2"};

uint32_t crc32(const unsigned char *data, size_t size)
{
    uint32_t crc = 0xffffffffU;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}
}

extern "C" {
volatile uint32_t dtln_model_verified, dtln_model_crc32;
volatile uint32_t dtln_ospi_hz, dtln_axi_hz;
volatile uint32_t dtln_model_address, dtln_model_in_sram;
volatile int32_t dtln_runtime_error;

int dtln_runtime_init(void)
{
    if (module) return dtln_runtime_error;
    int error = e8_flash_set_read_clock(APP_READ_CLOCK_HZ);
    if (error) return dtln_runtime_error = error;
    dtln_ospi_hz = APP_READ_CLOCK_HZ;
    dtln_axi_hz = GetSystemAXIClock();
    if (model_pte_size != MODEL_PTE_SIZE_BYTES) return dtln_runtime_error = -311;
    dtln_model_crc32 = crc32(model_pte, model_pte_size);
    if (dtln_model_crc32 != MODEL_PTE_CRC32) return dtln_runtime_error = -312;
    const unsigned char *runtime_pte = model_pte;
#if DTLN_MODEL_IN_SRAM
    std::memcpy(pte_cache, model_pte, model_pte_size);
    if (crc32(pte_cache, model_pte_size) != MODEL_PTE_CRC32) return dtln_runtime_error = -316;
    runtime_pte = pte_cache;
    dtln_model_in_sram = 1;
#endif
    dtln_model_verified = 1;
    dtln_model_address = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(runtime_pte));
    module = arm::embedded::initialize_runtime(runtime_pte, model_pte_size,
        method_pool, sizeof(method_pool), temp_pool, sizeof(temp_pool));
    if (!module) return dtln_runtime_error = -400;
#if defined(DTLN_USE_FAST_CPU_KERNELS)
    // Install the repository's existing guarded CPU kernels before any method loads.
    if (const int error = dtln_install_normalization(module); error)
        return dtln_runtime_error = error;
#endif
    return 0;
}

int dtln_forward(unsigned stage, const float *signal, const float *state,
                 const float *reference, float *output, float *next_state,
                 DtlnForwardTiming *timing)
{
    using namespace executorch::aten;
    if (timing) *timing = {};
    if (!module || stage < 1 || stage > 2 || !signal || !state || !reference ||
            !output || !next_state) return dtln_runtime_error = -400;
#if defined(DTLN_USE_FAST_CPU_KERNELS)
    dtln_quantization_current_method = stage - 1;
#endif
    const uint32_t start = osKernelGetSysTimerCount();
    uint32_t execute_ticks = 0;
    {
        const size_t width = stage == 1 ? 257 : 512;
        std::memcpy(input_signal, signal, width * sizeof(float));
        std::memcpy(input_reference, reference, width * sizeof(float));
        std::memcpy(input_state, state, sizeof(input_state));
        std::array<SizesType, 3> signal_shape{1, 1, static_cast<SizesType>(width)};
        std::array<SizesType, 4> state_shape{1, 2, 128, 2};
        std::array<DimOrderType, 3> signal_order{0, 1, 2};
        std::array<DimOrderType, 4> state_order{0, 1, 2, 3};
        TensorImpl signal_tensor(ScalarType::Float, 3, signal_shape.data(), input_signal, signal_order.data());
        TensorImpl state_tensor(ScalarType::Float, 4, state_shape.data(), input_state, state_order.data());
        TensorImpl reference_tensor(ScalarType::Float, 3, signal_shape.data(), input_reference, signal_order.data());
        std::vector<executorch::runtime::EValue> inputs{
            Tensor(&signal_tensor), Tensor(&state_tensor), Tensor(&reference_tensor)};
        const uint32_t execute_start = osKernelGetSysTimerCount();
        auto result = module->execute(method_names[stage - 1], inputs);
        execute_ticks = osKernelGetSysTimerCount() - execute_start;
        if (!result.ok()) return dtln_runtime_error = -500 - static_cast<int>(result.error());
        if (result->size() != 2) return dtln_runtime_error = -402;
        float *destinations[]{output, next_state};
        const size_t counts[]{width, DTLN_STATE_VALUES};
        for (unsigned index = 0; index < 2; ++index) {
            if (!(*result)[index].isTensor()) return dtln_runtime_error = -402;
            Tensor tensor = (*result)[index].toTensor();
            if (tensor.scalar_type() != ScalarType::Float || tensor.numel() != counts[index])
                return dtln_runtime_error = -402;
            std::memcpy(destinations[index], tensor.const_data_ptr<float>(), counts[index] * sizeof(float));
        }
    } // Include the local tensor/vector cleanup in the adapter measurement.
    if (timing) {
        timing->execute_ticks = execute_ticks;
        timing->adapter_ticks = osKernelGetSysTimerCount() - start;
    }
    return 0;
}

void dtln_runtime_memory(size_t *method_peak, size_t *temp_peak)
{
    const auto usage = arm::embedded::runtime_memory_usage();
    if (method_peak) *method_peak = usage.method_peak;
    if (temp_peak) *temp_peak = usage.temp_peak;
}
}
