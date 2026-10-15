// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "arm_embedded_module.hpp"
namespace arm::embedded {
struct RuntimeMemoryUsage { size_t method_peak, temp_peak; };
RuntimeMemoryUsage runtime_memory_usage();
// Initialize exactly once. All storage and PTE bytes must live until reset.
// The application owns pool sizes, placement and exclusive module use.
EmbeddedModule *initialize_runtime(const unsigned char *pte, size_t pte_size,
                                  uint8_t *method_pool, uint32_t method_size,
                                  uint8_t *temp_pool, uint32_t temp_size);
}
