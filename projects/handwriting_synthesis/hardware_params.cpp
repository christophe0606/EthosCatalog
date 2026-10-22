// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "hardware_params.h"
#include <cstdio>
#include "embedded_runtime.hpp"
#include "model_pte.h"
#include "board_nodes.h"
extern "C" int e8_flash_set_read_clock(uint32_t hz);
extern "C" uint32_t GetSystemAXIClock(void);
namespace {
alignas(32) uint8_t method_pool[APP_METHOD_POOL_SIZE] __attribute__((section(APP_POOL_SECTION)));
alignas(32) uint8_t temp_pool[APP_TEMP_POOL_SIZE] __attribute__((section(APP_POOL_SECTION)));
}
extern "C" {
StreamStats stream_stats;
volatile uint32_t model_verified, model_crc32;
volatile uint32_t handwriting_ospi_hz, handwriting_axi_hz;
volatile int32_t handwriting_clock_error;
int hardware_params_init(HardwareParams *params)
{
    if (!params) return -400;
    // Firmware executes in MRAM. No model read or inference is in flight here.
    // The bounded board API checks flash identity, DDR/dummy-cycle setup and
    // actual divider readback. Programming preparation retains its safe25MHz.
    handwriting_clock_error=e8_flash_set_read_clock(APP_READ_CLOCK_HZ);
    if(handwriting_clock_error) return -433;
    handwriting_ospi_hz=APP_READ_CLOCK_HZ; handwriting_axi_hz=GetSystemAXIClock();
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
    params->uart_read=board_uart_rx_read;
    params->lcd_init=lcd_init_display; params->lcd_acquire=lcd_wait_for_release;
    params->lcd_present=lcd_submit_region;
    if(board_uart_rx_start()) return -432;
    printf("OSPI read clock=%lu Hz AXI=%lu Hz divider=%lu; model CRC=%08lx PASS\n",
        (unsigned long)handwriting_ospi_hz,(unsigned long)handwriting_axi_hz,
        (unsigned long)(handwriting_axi_hz/handwriting_ospi_hz),(unsigned long)model_crc32);
    return 0;
}
void hardware_params_uninit(HardwareParams *params) { if(params) *params={}; }
}
