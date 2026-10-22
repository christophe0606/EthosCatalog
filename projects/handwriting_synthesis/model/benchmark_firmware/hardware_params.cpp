// SPDX-License-Identifier: Apache-2.0
#include "hardware_params.h"
#include "embedded_runtime.hpp"
#include "model_pte.h"
namespace {
alignas(32) uint8_t method_pool[APP_METHOD_POOL_SIZE] __attribute__((section(APP_POOL_SECTION)));
alignas(32) uint8_t temp_pool[APP_TEMP_POOL_SIZE] __attribute__((section(APP_POOL_SECTION)));
}
extern "C" {
StreamStats stream_stats;
volatile uint32_t model_verified, model_crc32;
int hardware_params_init(HardwareParams *params)
{
    if (!params) return -400;
    uint32_t crc=0xffffffffU;
    for(size_t i=0;i<model_pte_size;++i) {
        crc^=model_pte[i];
        for(unsigned b=0;b<8;++b) crc=(crc>>1)^(0xedb88320U&(0U-(crc&1U)));
    }
    model_crc32=~crc;
    if(model_pte_size!=MODEL_PTE_SIZE_BYTES || model_crc32!=MODEL_PTE_CRC32) return -311;
    model_verified=1;
    auto *module=arm::embedded::initialize_runtime(model_pte,model_pte_size,
        method_pool,sizeof(method_pool),temp_pool,sizeof(temp_pool));
    if(!module) return -400;
    *params={}; params->module=module; params->stats=&stream_stats;
    params->model_size=model_pte_size;
    params->method_pool_size=sizeof(method_pool); params->temp_pool_size=sizeof(temp_pool);
    return 0;
}
void hardware_params_uninit(HardwareParams *params) { if(params) *params={}; }
}
