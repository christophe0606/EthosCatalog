// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
//
// Headless ExecuTorch runner for the Ethos-U85 example. Loads the .pte
// embedded by the AI layer (model_pte.h, see create_ai_layer.py), benchmarks
// warmed NPU inferences, and checks each against the reference output. Built entirely from the
// PyTorch::ExecuTorch pack's runtime + operator components; the board layer
// provides main(), stdout and the Ethos-U driver init and then calls app_main.
//
// EmbeddedModule (arm_embedded_module.hpp) manages program loading, method
// memory and execution, like ExecuTorch's Module class does on POSIX hosts.

#include <array>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <memory>

#include <executorch/extension/data_loader/buffer_data_loader.h>
#include <executorch/runtime/core/evalue.h>
#include <executorch/runtime/core/exec_aten/exec_aten.h>
#include <executorch/runtime/core/memory_allocator.h>
#include <executorch/runtime/platform/runtime.h>

#include "arm_embedded_module.hpp"
#include "model_pte.h"
#include "model/model_test_data.h"
#include "main.h"
#include "cmsis_os2.h"
#include "se_services_port.h"

using arm::embedded::EmbeddedModule;
using executorch::aten::DimOrderType;
using executorch::aten::ScalarType;
using executorch::aten::SizesType;
using executorch::aten::Tensor;
using executorch::aten::TensorImpl;
using executorch::extension::BufferDataLoader;
using executorch::runtime::EValue;
using executorch::runtime::MemoryAllocator;

namespace {

// The project sets pool sizes and the section its linker routes to
// NPU-accessible memory. These defaults fit the template smoke model.
#ifndef APP_METHOD_POOL_SIZE
#define APP_METHOD_POOL_SIZE (64 * 1024)
#endif
#ifndef APP_TEMP_POOL_SIZE
#define APP_TEMP_POOL_SIZE (64 * 1024)  // Ethos-U scratch is drawn from here.
#endif
#ifdef APP_POOL_SECTION
#define APP_POOL_ATTRIBUTES __attribute__((section(APP_POOL_SECTION)))
#else
#define APP_POOL_ATTRIBUTES
#endif

constexpr size_t kMethodPoolSize = APP_METHOD_POOL_SIZE;
constexpr size_t kTempPoolSize = APP_TEMP_POOL_SIZE;

alignas(16) uint8_t g_method_pool[kMethodPoolSize] APP_POOL_ATTRIBUTES;
alignas(16) uint8_t g_temp_pool[kTempPoolSize] APP_POOL_ATTRIBUTES;

// Track temporary allocations before ExecuTorch resets the allocator each op.
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
uint64_t g_benchmark_cycles[kBenchmarkSamples];
uint32_t g_benchmark_forward_ticks[kBenchmarkSamples];
volatile uint32_t g_benchmark_complete;

}  // namespace

extern "C" int app_main(void) {
  executorch::runtime::runtime_init();

  printf("ExecuTorch Ethos-U85 example: %lu byte model\n", model_pte_size);

  uint32_t cpu_hz = 0, axi_hz = 0, service_error = 0;
  if (SERVICES_clocks_setting_get(se_services_s_handle, CLOCK_SETTING_EXTSYS0_FREQ,
                                 &cpu_hz, &service_error) != SERVICES_REQ_SUCCESS ||
      service_error || !cpu_hz) return 1;
  if (SERVICES_clocks_setting_get(se_services_s_handle, CLOCK_SETTING_AXI_FREQ,
                                 &axi_hz, &service_error) != SERVICES_REQ_SUCCESS ||
      service_error || !axi_hz) return 1;

  auto method_allocator = std::make_unique<PeakAllocator>(kMethodPoolSize, g_method_pool);
  auto temp_allocator = std::make_unique<PeakAllocator>(kTempPoolSize, g_temp_pool);
  auto* method_usage = method_allocator.get();
  auto* temp_usage = temp_allocator.get();

  EmbeddedModule module(
      model_pte, model_pte_size,
      std::make_unique<BufferDataLoader>(model_pte, model_pte_size),
      std::move(method_allocator), std::move(temp_allocator));

  // SmokeModel takes an 8x8 single-channel NHWC image. This project owns
  // the fixed input and reference output in model/model_test_data.h.
  alignas(16) static float input_data[64] APP_POOL_ATTRIBUTES;
  static_assert(sizeof(input_data) == sizeof(model_test_input));
  std::memcpy(input_data, model_test_input, sizeof(input_data));
  std::array<SizesType, 4> sizes{model_input_shape[0], model_input_shape[1],
                               model_input_shape[2], model_input_shape[3]};
  std::array<DimOrderType, 4> dim_order{0, 1, 2, 3};
  TensorImpl input_impl(ScalarType::Float, sizes.size(), sizes.data(), input_data, dim_order.data());
  Tensor input(&input_impl);

  const uint32_t timer_hz = osKernelGetSysTimerFreq();
  float overall_max_error = 0.0f;
  // First call loads the method and warms caches. Validate it and every sample.
  for (unsigned sample = 0; sample <= kBenchmarkSamples; ++sample) {
    ethos_benchmark_reset();
    const uint32_t start = osKernelGetSysTimerCount();
    auto outputs = module.forward(input);
    const uint32_t elapsed = osKernelGetSysTimerCount() - start;
    if (!outputs.ok()) {
      printf("forward failed (err=%u)\n", static_cast<unsigned>(outputs.error()));
      return 1;
    }
    if (outputs->empty()) {
      printf("forward returned no outputs\n");
      return 1;
    }
    if (!ethos_benchmark.invocations || !ethos_benchmark.cycles ||
        ethos_benchmark.overflow ||
        ethos_benchmark.cycles != ethos_benchmark.active_cycles) {
      printf("Test_result: FAIL (invalid Ethos PMU measurement)\n");
      return 1;
    }
    if (sample) {
      g_benchmark_cycles[sample - 1] = ethos_benchmark.cycles;
      g_benchmark_forward_ticks[sample - 1] = elapsed;
    }

    const EValue& out = outputs->front();
    if (!out.isTensor()) {
      printf("Test_result: FAIL (output is not a tensor)\n");
      return 1;
    }
    Tensor t = out.toTensor();
    constexpr size_t expected_count = sizeof(model_test_expected) / sizeof(float);
    if (t.scalar_type() != ScalarType::Float || t.numel() != expected_count) {
      printf("Test_result: FAIL (output type or size)\n");
      return 1;
    }
    const float* p = t.const_data_ptr<float>();
    float max_error = 0.0f;
    for (size_t i = 0; i < expected_count; ++i) {
      const float error = std::fabs(p[i] - model_test_expected[i]);
      // Inspect float32 exponent bits: -ffast-math assumes isfinite() is true.
      uint32_t bits;
      std::memcpy(&bits, p + i, sizeof(bits));
      const bool finite = (bits & 0x7f800000U) != 0x7f800000U;
      if (!finite || error > model_test_tolerance) {
        printf("Test_result: FAIL (element %u, %.4f vs %.4f)\n",
               static_cast<unsigned>(i), p[i], model_test_expected[i]);
        return 1;
      }
      if (error > max_error) max_error = error;
    }
    if (max_error > overall_max_error) overall_max_error = max_error;
  }
  printf("Output: %u elements, max absolute error %.6f\n",
         static_cast<unsigned>(sizeof(model_test_expected) / sizeof(float)), overall_max_error);
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
         static_cast<unsigned>(kMethodPoolSize + kTempPoolSize));
  for (unsigned sample = 0; sample < kBenchmarkSamples; ++sample) {
    printf("Benchmark sample %u: NPU=%llu cycles (%.6f ms), forward=%lu ticks (%.6f ms)\n",
           sample + 1, static_cast<unsigned long long>(g_benchmark_cycles[sample]),
           1000.0 * g_benchmark_cycles[sample] / axi_hz,
           static_cast<unsigned long>(g_benchmark_forward_ticks[sample]),
           1000.0 * g_benchmark_forward_ticks[sample] / timer_hz);
  }
  printf("Benchmark PMU: last active=%llu cycles, invocations=%lu, overflow=%lu\n",
         static_cast<unsigned long long>(ethos_benchmark.active_cycles),
         static_cast<unsigned long>(ethos_benchmark.invocations),
         static_cast<unsigned long>(ethos_benchmark.overflow));
  g_benchmark_complete = 1;
  printf("Test_result: PASS\n");
  fflush(stdout);
  return 0;
}
