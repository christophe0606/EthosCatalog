// SPDX-License-Identifier: Apache-2.0
#include "embedded_runtime.hpp"
#include "embedded_gain.h"
#include <executorch/extension/data_loader/buffer_data_loader.h>
#include <executorch/runtime/platform/runtime.h>
#include <new>
#include <array>
#include <cstring>
#include "cmsis_os2.h"
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
int gain_forward(EmbeddedModule *module, float *input, float *output,
                 size_t count, uint32_t *forward_ticks)
{
    using namespace executorch::aten;
    if (!module || !input || !output || !count) return -400;
    std::array<SizesType, 4> sizes{1, 1, static_cast<SizesType>(count), 1};
    std::array<DimOrderType, 4> order{0, 1, 2, 3};
    TensorImpl impl(ScalarType::Float, sizes.size(), sizes.data(), input, order.data());
    Tensor tensor(&impl);
    uint32_t start = osKernelGetSysTimerCount();
    auto result = module->forward(tensor);
    if (forward_ticks) *forward_ticks = osKernelGetSysTimerCount() - start;
    if (!result.ok() || result->size() != 1 || !result->front().isTensor()) return -401;
    Tensor out = result->front().toTensor();
    if (out.scalar_type() != ScalarType::Float || out.numel() != count) return -402;
    std::memcpy(output, out.const_data_ptr<float>(), count * sizeof(float));
    return 0;
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
extern "C" int embedded_gain_forward(void *module, float *input, float *output, size_t count)
{
    return arm::embedded::gain_forward(static_cast<arm::embedded::EmbeddedModule*>(module), input, output, count);
}
