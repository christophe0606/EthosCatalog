// SPDX-License-Identifier: Apache-2.0
#include "hardware_params.h"
#include "embedded_runtime.hpp"
#include "board_nodes.h"
#include "spectrum.h"
#include "model_pte.h"
#include <cstring>
namespace {
alignas(32) uint8_t method_pool[APP_METHOD_POOL_SIZE] __attribute__((section(APP_POOL_SECTION)));
alignas(32) uint8_t temp_pool[APP_TEMP_POOL_SIZE] __attribute__((section(APP_POOL_SECTION)));
alignas(32) float inference_input[MIC_BLOCK_SAMPLES] __attribute__((section(APP_POOL_SECTION)));
alignas(32) uint16_t framebuffers[2][LCD_PIXELS] __attribute__((section(".bss.lcd_frame_buf")));
int read_microphone_samples(int16_t *samples)
{
    // The DevKit-E8 API also serves applications that use capture metadata.
    bool discontinuity;
    return microphone_read(samples, &discontinuity);
}
}
extern "C" {
StreamStats stream_stats;
int hardware_params_init(HardwareParams *params)
{
    if (!params) return -400;
    auto* module = arm::embedded::initialize_runtime(model_pte, model_pte_size,
        method_pool, sizeof(method_pool), temp_pool, sizeof(temp_pool));
    if (!module) return -400;
    *params = {};
    params->module = module;
    params->inference_input = inference_input;
    params->inference_samples = sizeof(inference_input)/sizeof(float);
    params->stats = &stream_stats;
    params->microphone_start = microphone_start;
    params->microphone_read = read_microphone_samples;
    params->microphone_block_samples = MICROPHONE_BLOCK_SAMPLES;
    params->framebuffers[0] = framebuffers[0]; params->framebuffers[1] = framebuffers[1];
    params->lcd_touch = lcd_touch; params->lcd_present = lcd_present;
    params->ui_touch = spectrum_touch; params->ui_release = spectrum_touch_release;
    params->ui_render = spectrum_render;
    params->model_size = model_pte_size;
    params->method_pool_size = sizeof(method_pool); params->temp_pool_size = sizeof(temp_pool);
    spectrum_ui_reset();
    spectrum_render(framebuffers[0], nullptr, true);
    return lcd_init(framebuffers[0]);
}
void hardware_params_uninit(HardwareParams *params)
{
    // Drivers and model storage live until reset. Call only after graph teardown.
    if (params) *params = {};
}
}
