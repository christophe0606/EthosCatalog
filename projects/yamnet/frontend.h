// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once
typedef struct log_mel_config log_mel_config_t;
#ifdef __cplusplus
extern "C" {
#endif
extern const log_mel_config_t yamnet_log_mel_config;
int yamnet_frontend_selftest(void);
extern float yamnet_frontend_max_error;
extern volatile unsigned yamnet_frontend_passed;
#ifdef __cplusplus
}
#endif
