// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
// The MRAM template's smoke model, stored separately in OSPI. Stage and verify
// its PTE in SRAM0 before a single checked inference; no timing/benchmark loop.
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
alignas(32) uint8_t staged_model[MODEL_PTE_SIZE_BYTES]
    __attribute__((section(".bss.ai_model_pool")));
alignas(32) uint8_t method_pool[APP_METHOD_POOL_SIZE]
    __attribute__((section(APP_POOL_SECTION)));
alignas(32) uint8_t temp_pool[APP_TEMP_POOL_SIZE]
    __attribute__((section(APP_POOL_SECTION)));
alignas(32) float input_data[64] __attribute__((section(APP_POOL_SECTION)));

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
  if (model_pte_size != sizeof(staged_model)) return -310;
  std::memcpy(staged_model, model_pte, sizeof(staged_model));
  model_crc32 = crc32(staged_model, sizeof(staged_model));
  if (model_crc32 != MODEL_PTE_CRC32) {
    printf("Test_result: FAIL (OSPI model CRC mismatch)\n");
    return -311;
  }
  model_verified = 1;
  SCB_CleanDCache();
  __DSB();
  executorch::runtime::runtime_init();
  EmbeddedModule module(
      staged_model, sizeof(staged_model),
      std::make_unique<BufferDataLoader>(staged_model, sizeof(staged_model)),
      std::make_unique<MemoryAllocator>(sizeof(method_pool), method_pool),
      std::make_unique<MemoryAllocator>(sizeof(temp_pool), temp_pool));

  static_assert(sizeof(input_data) == sizeof(model_test_input));
  std::memcpy(input_data, model_test_input, sizeof(input_data));
  std::array<SizesType, 4> sizes{model_input_shape[0], model_input_shape[1],
                               model_input_shape[2], model_input_shape[3]};
  std::array<DimOrderType, 4> order{0, 1, 2, 3};
  TensorImpl impl(ScalarType::Float, sizes.size(), sizes.data(), input_data, order.data());
  Tensor input(&impl);
  auto outputs = module.forward(input);
  if (!outputs.ok() || outputs->empty() || !outputs->front().isTensor()) {
    printf("Test_result: FAIL (forward/output)\n");
    return -312;
  }
  Tensor output = outputs->front().toTensor();
  constexpr size_t count = sizeof(model_test_expected) / sizeof(float);
  if (output.scalar_type() != ScalarType::Float || output.numel() != count) return -313;
  const float* values = output.const_data_ptr<float>();
  float max_error = 0.0f;
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
  printf("Output: %u elements, max absolute error %.6f\n", static_cast<unsigned>(count), max_error);
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
