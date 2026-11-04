// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
// Compiled through the pack's RTE/CMSIS/stream_init.cpp configuration shim.
#include "app_config.hpp"
#include "scheduler_app.h"
#include "stream_runtime_init.hpp"
#include "stream_init.hpp"
#include "main.h"
using namespace arm_cmsis_stream;
extern "C" volatile int32_t pipeline_error;
namespace {
EventQueue *queue;
stream_execution_context_t context;
bool application_handler(int, void*, Event&& event)
{
    if (event.event_id == kError && event.wellFormed<int32_t,int32_t,int32_t>()) {
        event.apply<int32_t,int32_t,int32_t>([](int32_t, int32_t error, int32_t) {
            stream_stats.error = error;
            pipeline_error = error;
            app_result = error;
        });
    }
    return true;
}
}
int stream_configure_and_start(const HardwareParams *hardware)
{
    if (!hardware) return CG_INIT_FAILURE;
    app_params_set_hardware(hardware);
    int error = stream_init_memory();
    if (error) return error;
    queue = stream_new_event_queue();
    if (!queue) return CG_MEMORY_ALLOCATION_FAILURE;
    queue->setHandler(nullptr, application_handler);
    error = init_scheduler_app(queue, &appParams);
    if (error != CG_SUCCESS) return error;
    context = {scheduler_app, reset_fifos_scheduler_app, nullptr, nullptr, nullptr,
               queue, STREAM_APP_NB_IDENTIFIED_NODES, STREAM_APP_SCHED_LEN};
    if (!stream_start_threads(&context)) return CG_OS_ERROR;
    return 0;
}
