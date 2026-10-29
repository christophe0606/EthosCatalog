// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "hardware_params.h"
#include "app_sizes.h"
#include "board_nodes.h"
#include "embedded_runtime.hpp"
#include "model_pte.h"
#include "nodes_impl/yamnet_ui.h"
#include "helium/convolution.h"
#include <cstring>
extern "C" int e8_flash_set_read_clock(uint32_t hz);
namespace {
alignas(32) uint8_t method_pool[APP_METHOD_POOL_SIZE] __attribute__((section(APP_POOL_SECTION)));
alignas(32) uint8_t temp_pool[APP_TEMP_POOL_SIZE] __attribute__((section(APP_POOL_SECTION)));
alignas(32) uint16_t framebuffers[2][LCD_PIXELS] __attribute__((section(".bss.lcd_frame_buf")));
int start_microphone() { return microphone_start_configured(16000, 160); }
int read_microphone(int16_t *out) {
    bool gap = false;
    int e = microphone_read(out, &gap);
    if (gap) {
        ++stream_stats.discontinuities;
        return -451;
    }
    return e;
}
} // namespace
extern "C" {
volatile uint32_t model_verified, model_crc32;
volatile int32_t yamnet_clock_error;
int hardware_params_init(HardwareParams *params) {
    if (!params)
        return -400;
    yamnet_clock_error = e8_flash_set_read_clock(100000000U);
    if (yamnet_clock_error)
        return -433;
    uint32_t crc = 0xffffffffU;
    for (size_t i = 0; i < model_pte_size; ++i) {
        crc ^= model_pte[i];
        for (unsigned b = 0; b < 8; ++b)
            crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    model_crc32 = ~crc;
    if (model_pte_size != MODEL_PTE_SIZE_BYTES || model_crc32 != MODEL_PTE_CRC32)
        return -311;
    model_verified = 1;
    auto *module = arm::embedded::initialize_runtime(
        model_pte, model_pte_size, method_pool, sizeof(method_pool), temp_pool, sizeof(temp_pool));
    if (!module)
        return -400;
    if (int error = yamnet_install_helium(module))
        return error;
    *params = {};
    params->module = module;
    params->stats = &stream_stats;
    params->model_size = model_pte_size;
    params->method_pool_size = sizeof(method_pool);
    params->temp_pool_size = sizeof(temp_pool);
    params->microphone_start = start_microphone;
    params->microphone_read = read_microphone;
    params->microphone_block_samples = 160;
    params->framebuffers[0] = framebuffers[0];
    params->framebuffers[1] = framebuffers[1];
    params->lcd_present = lcd_present;
    yamnet_render(framebuffers[0], nullptr, nullptr, &stream_stats);
    return lcd_init_display(framebuffers[0]);
}
void hardware_params_uninit(HardwareParams *params) {
    if (params)
        *params = {};
}
}
