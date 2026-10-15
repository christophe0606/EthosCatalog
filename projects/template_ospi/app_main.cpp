// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
// The MRAM template's smoke model, read directly from memory-mapped OSPI.
// Verify the PTE in place, then benchmark and check warmed inferences.
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>

#include <executorch/extension/data_loader/buffer_data_loader.h>
#include <executorch/runtime/core/evalue.h>
#include <executorch/runtime/core/exec_aten/exec_aten.h>
#include <executorch/runtime/core/memory_allocator.h>
#include <executorch/runtime/platform/runtime.h>

#include "RTE_Components.h"
#include CMSIS_device_header
#include "arm_embedded_module.hpp"
#include "main.h"
#include "cmsis_os2.h"
#include "se_services_port.h"
#include "model_pte.h"
#include "model/model_test_data.h"

extern "C" {
extern volatile uint32_t app_stage;
extern volatile int32_t app_error;
volatile uint32_t model_crc32;
volatile uint32_t model_verified;
}

using arm::embedded::EmbeddedModule;
using executorch::aten::DimOrderType;
using executorch::aten::ScalarType;
using executorch::aten::SizesType;
using executorch::aten::Tensor;
using executorch::aten::TensorImpl;
using executorch::extension::BufferDataLoader;
using executorch::runtime::MemoryAllocator;

namespace {
alignas(32) uint8_t method_pool[APP_METHOD_POOL_SIZE]
    __attribute__((section(APP_POOL_SECTION)));
alignas(32) uint8_t temp_pool[APP_TEMP_POOL_SIZE]
    __attribute__((section(APP_POOL_SECTION)));
alignas(32) float input_data[64] __attribute__((section(APP_POOL_SECTION)));

class PeakAllocator final : public MemoryAllocator {
 public:
  PeakAllocator(uint32_t size, uint8_t* base) : MemoryAllocator(size, base) {}
  void* allocate(size_t size, size_t alignment = kDefaultAlignment) override {
    void* result = MemoryAllocator::allocate(size, alignment);
    if (used_size() > peak) peak = used_size();
    return result;
  }
  size_t peak = 0;
};

constexpr unsigned kBenchmarkSamples = 10;
uint64_t benchmark_cycles[kBenchmarkSamples];
uint32_t benchmark_forward_ticks[kBenchmarkSamples];
volatile uint32_t benchmark_complete;

uint32_t crc32(const uint8_t* bytes, size_t count) {
  uint32_t crc = 0xffffffffU;
  for (size_t i = 0; i < count; ++i) {
    crc ^= bytes[i];
    for (unsigned bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
  }
  return ~crc;
}

int run_inference() {
  // app_prepare has enabled XIP. Never read OSPI from the ProgramModel build.
  if (model_pte_size != MODEL_PTE_SIZE_BYTES) return -310;
  model_crc32 = crc32(model_pte, model_pte_size);
  if (model_crc32 != MODEL_PTE_CRC32) {
    printf("Test_result: FAIL (OSPI model CRC mismatch)\n");
    return -311;
  }
  model_verified = 1;
  SCB_CleanDCache();
  __DSB();
  executorch::runtime::runtime_init();
  uint32_t cpu_hz = 0, axi_hz = 0, service_error = 0;
  if (SERVICES_clocks_setting_get(se_services_s_handle, CLOCK_SETTING_EXTSYS0_FREQ,
                                 &cpu_hz, &service_error) != SERVICES_REQ_SUCCESS ||
      service_error || !cpu_hz) return -315;
  if (SERVICES_clocks_setting_get(se_services_s_handle, CLOCK_SETTING_AXI_FREQ,
                                 &axi_hz, &service_error) != SERVICES_REQ_SUCCESS ||
      service_error || !axi_hz) return -315;
  const uint32_t timer_hz = osKernelGetSysTimerFreq();
  if (!timer_hz) return -315;
  auto method_allocator = std::make_unique<PeakAllocator>(sizeof(method_pool), method_pool);
  auto temp_allocator = std::make_unique<PeakAllocator>(sizeof(temp_pool), temp_pool);
  auto* method_usage = method_allocator.get();
  auto* temp_usage = temp_allocator.get();
  EmbeddedModule module(
      model_pte, model_pte_size,
      std::make_unique<BufferDataLoader>(model_pte, model_pte_size),
      std::move(method_allocator), std::move(temp_allocator));

  static_assert(sizeof(input_data) == sizeof(model_test_input));
  std::memcpy(input_data, model_test_input, sizeof(input_data));
  std::array<SizesType, 4> sizes{model_input_shape[0], model_input_shape[1],
                               model_input_shape[2], model_input_shape[3]};
  std::array<DimOrderType, 4> order{0, 1, 2, 3};
  TensorImpl impl(ScalarType::Float, sizes.size(), sizes.data(), input_data, order.data());
  Tensor input(&impl);
  constexpr size_t count = sizeof(model_test_expected) / sizeof(float);
  float max_error = 0.0f;
  // Exclude method loading and the first inference; validate every result.
  for (unsigned sample = 0; sample <= kBenchmarkSamples; ++sample) {
    ethos_benchmark_reset();
    const uint32_t start = osKernelGetSysTimerCount();
    auto outputs = module.forward(input);
    const uint32_t elapsed = osKernelGetSysTimerCount() - start;
    if (!outputs.ok() || outputs->empty() || !outputs->front().isTensor()) {
      printf("Test_result: FAIL (forward/output)\n");
      return -312;
    }
    if (ethos_benchmark.invocations != 1 || !ethos_benchmark.cycles ||
        ethos_benchmark.overflow ||
        ethos_benchmark.cycles != ethos_benchmark.active_cycles) {
      printf("Test_result: FAIL (invalid Ethos PMU measurement)\n");
      return -316;
    }
    if (sample) {
      benchmark_cycles[sample - 1] = ethos_benchmark.cycles;
      benchmark_forward_ticks[sample - 1] = elapsed;
    }
    Tensor output = outputs->front().toTensor();
    if (output.scalar_type() != ScalarType::Float || output.numel() != count) return -313;
    const float* values = output.const_data_ptr<float>();
    for (size_t i = 0; i < count; ++i) {
      uint32_t bits;
      std::memcpy(&bits, values + i, sizeof(bits));
      const float error = std::fabs(values[i] - model_test_expected[i]);
      if ((bits & 0x7f800000U) == 0x7f800000U || error > model_test_tolerance) {
        printf("Test_result: FAIL (element %u)\n", static_cast<unsigned>(i));
        return -314;
      }
      if (error > max_error) max_error = error;
    }
  }
  printf("Output: %u elements, max absolute error %.6f\n", static_cast<unsigned>(count), max_error);
  printf("Benchmark clocks: CPU=%lu Hz, AXI/NPU=%lu Hz, timer=%lu Hz\n",
         static_cast<unsigned long>(cpu_hz), static_cast<unsigned long>(axi_hz),
         static_cast<unsigned long>(timer_hz));
  printf("Benchmark memory: model=%lu B, arena=%lu B at 0x%08lx, weights=%lu B at 0x%08lx\n",
         model_pte_size, static_cast<unsigned long>(ethos_benchmark.arena_size),
         static_cast<unsigned long>(ethos_benchmark.arena_address),
         static_cast<unsigned long>(ethos_benchmark.weights_size),
         static_cast<unsigned long>(ethos_benchmark.weights_address));
  printf("Benchmark pools: method peak=%u B, temp peak=%u B, reserved=%u B\n",
         static_cast<unsigned>(method_usage->peak), static_cast<unsigned>(temp_usage->peak),
         static_cast<unsigned>(sizeof(method_pool) + sizeof(temp_pool)));
  for (unsigned sample = 0; sample < kBenchmarkSamples; ++sample) {
    printf("Benchmark sample %u: NPU=%llu cycles (%.6f ms), forward=%lu ticks (%.6f ms)\n",
           sample + 1, static_cast<unsigned long long>(benchmark_cycles[sample]),
           1000.0 * benchmark_cycles[sample] / axi_hz,
           static_cast<unsigned long>(benchmark_forward_ticks[sample]),
           1000.0 * benchmark_forward_ticks[sample] / timer_hz);
  }
  printf("Benchmark PMU: last active=%llu cycles, invocations=%lu, overflow=%lu\n",
         static_cast<unsigned long long>(ethos_benchmark.active_cycles),
         static_cast<unsigned long>(ethos_benchmark.invocations),
         static_cast<unsigned long>(ethos_benchmark.overflow));
  benchmark_complete = 1;
  printf("Test_result: PASS\n");
  fflush(stdout);
  return 0;
}
}  // namespace

extern "C" int app_main(void) {
  app_error = run_inference();
  app_stage = app_error ? 0xFFFFFFFFU : 4U;
  return app_error;
}
