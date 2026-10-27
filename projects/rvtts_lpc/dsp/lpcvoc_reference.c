// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
// Build the preserved rvTTS scalar implementation under separate symbols.
#define lpcvoc_init lpcvoc_reference_init
#define lpcvoc_reset lpcvoc_reference_reset
#define lpcvoc_frame lpcvoc_reference_frame
#include "../upstream/lpcvoc.c"
