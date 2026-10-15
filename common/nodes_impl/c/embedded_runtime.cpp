// SPDX-License-Identifier: Apache-2.0
#include "embedded_runtime.hpp"
#include <executorch/extension/data_loader/buffer_data_loader.h>
#include <executorch/runtime/platform/runtime.h>
#include <new>
namespace arm::embedded {
namespace {
class PeakAllocator final : public executorch::runtime::MemoryAllocator {
public:
    PeakAllocator(uint32_t size, uint8_t *base) : MemoryAllocator(size, base) {}
    void *allocate(size_t size, size_t alignment = kDefaultAlignment) override {
        void *result = MemoryAllocator::allocate(size, alignment);
        if (used_size() > peak) peak = used_size();
        return result;
    }
    size_t peak = 0;
};
PeakAllocator *method_usage, *temp_usage;
}
RuntimeMemoryUsage runtime_memory_usage()
{
    return {method_usage ? method_usage->peak : 0, temp_usage ? temp_usage->peak : 0};
}
EmbeddedModule *initialize_runtime(const unsigned char *pte, size_t pte_size,
                                  uint8_t *method_pool, uint32_t method_size,
                                  uint8_t *temp_pool, uint32_t temp_size)
{
    static bool initialized;
    alignas(EmbeddedModule) static unsigned char storage[sizeof(EmbeddedModule)];
    if (initialized || !pte || !pte_size || !method_pool || !temp_pool) return nullptr;
    executorch::runtime::runtime_init();
    auto method = std::make_unique<PeakAllocator>(method_size, method_pool);
    auto temp = std::make_unique<PeakAllocator>(temp_size, temp_pool);
    method_usage = method.get(); temp_usage = temp.get();
    auto *module = new (storage) EmbeddedModule(pte, pte_size,
        std::make_unique<executorch::extension::BufferDataLoader>(pte, pte_size),
        std::move(method), std::move(temp));
    initialized = true;
    // Intentionally no destructor: the runtime and its allocators live until reset.
    return module;
}
}
