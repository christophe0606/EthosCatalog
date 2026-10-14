/* Copyright (c) 2026 Arm Limited or its affiliates. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 * Generated with AI assistance for this demo.
 * Apache-2.0 applies only to new Arm code, not imported model data or upstream content.
 * See LICENSE, LICENSING.md and THIRD_PARTY_NOTICES.md for scope and provenance.
 */

/* FlashOS descriptor format: Arm CMSIS, Apache-2.0 (see FlashOS.h).
 * Expose only the 32 MiB model partition. Never provide chip erase.
 */
#include "FlashOS.h"
__attribute__((section("DevDscr")))
const struct FlashDevice FlashDevice = {
    FLASH_DRV_VERS, "Alif DevKit-E8 ISSI model partition", EXTSPI,
    0xC0000000UL, 0x02000000UL, 256UL, 0UL, 0xFF,
    1000UL, 5000UL,
    {{4096UL, 0UL}, {0xFFFFFFFFUL, 0xFFFFFFFFUL}}
};
