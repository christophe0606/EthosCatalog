---
id: techniques-armv8m-cache-management
title: Manage Cortex-M55 and Cortex-M85 caches with Armv8-M enable and deactivate semantics
type: techniques
architectures:
  - armv8.1-m
  - cortex-m55
  - cortex-m85
tags:
  - cache-management
  - cache
  - processor-initialization
  - c
confidence: source-reported
reproducibility: pseudocode
related:
  - techniques-armv81m-feature-initialization
  - hardware-itcm-instruction-data-contention
  - hardware-dtcm-scatter-gather-banks
sources:
  - https://developer.arm.com/community/arm-community-blogs/b/architectures-and-processors-blog/posts/armv8_2d00_m-based-processor-software-development-hints-and-tips
aliases:
  - Cortex-M cache enable
  - Cortex-M cache disable
  - Armv8-M cache management
  - SCB CCR MSCR
  - cache PMU events
---

On M55/M85 systems with implemented caches, enable I-cache and D-cache for execution from slow flash or high-latency AXI memory, and enable D-cache for AXI SRAM data where appropriate. The [Armv8.1-M software development hints and tips article](https://developer.arm.com/community/arm-community-blogs/b/architectures-and-processors-blog/posts/armv8_2d00_m-based-processor-software-development-hints-and-tips) says caches are disabled at reset. Cache operations and the distinction between disabling allocation and fully deactivating a cache matter when bringing up firmware or porting code that assumed Armv7-M behavior.

For the M55/M85 hardware reset-invalidation path, setting the instruction/data cache bits `SCB->CCR.IC` and `SCB->CCR.DC` can enable the caches without first running software invalidation, provided the hardware integration has automatic reset invalidation enabled. Follow setting the CCR bits with `__DSB();` and then `__ISB();`. The I-cache and D-cache invalidations can proceed in parallel; the DSB waits for automatic invalidation to complete, and cache-maintenance operations during that sequence behave as no-ops. Hardware can disable this mechanism to preserve cache contents across reset, so verify the integration before relying on this startup path. CMSIS `SCB_EnableICache()` and `SCB_EnableDCache()` perform software invalidation as part of their enable sequence.

On Armv8-M, clearing `CCR.IC` or `CCR.DC` stops new cache-line allocation but does not necessarily stop lookup of valid lines. To fully deactivate the caches, use this order:

1. Clear the relevant `SCB->CCR` allocation bit or bits.
2. Clean and invalidate D-cache (so dirty data is written back), and invalidate I-cache as needed.
3. Wait for cache maintenance to complete and apply the required barriers.
4. Clear `SCB->MSCR` `ICACTIVE` and/or `DCACTIVE` to deactivate cache lookup, then execute `__DSB();` followed by `__ISB();`.

After the cache-maintenance operations in step 2, execute `__DSB();` followed by `__ISB();` before clearing the `xACTIVE` bit(s) in step 4.

The [Armv8.1-M software development hints and tips article](https://developer.arm.com/community/arm-community-blogs/b/architectures-and-processors-blog/posts/armv8_2d00_m-based-processor-software-development-hints-and-tips)'s prose calls the Memory System Control Register “MCSR” once, while its code example uses `MSCR`; use the CMSIS `SCB->MSCR` spelling and confirm available fields and macros in the device headers. The ordering above describes the Armv8.1-M software development hints and tips article's example sequence; select the relevant cache operations for the caches present in the system.

This distinction also affects performance-counter interpretation on Cortex-M55: revision r0 can still report cache-access events after the cache is deactivated because cache logic remains in the access path. Revision r1 masks these events when the caches are deactivated in `MSCR`. The [Armv8.1-M software development hints and tips article](https://developer.arm.com/community/arm-community-blogs/b/architectures-and-processors-blog/posts/armv8_2d00_m-based-processor-software-development-hints-and-tips) lists data-cache-related events `0x0003 L1D_CACHE`, `0x0036 LL_CACHE_RD`, `0x0037 LL_CACHE_MISS_RD`, `0x0039 L1D_CACHE_MISS_RD`, and `0x0040 L1D_CACHE_RD`; this revision behavior is specific to M55 and should not be generalized to M85.
