---
id: hardware-itcm-instruction-data-contention
title: Avoid instruction and constant-data contention in Cortex-M55 and Cortex-M85 ITCM
type: hardware
architectures:
  - cortex-m55
  - cortex-m85
tags:
  - instruction-tcm
  - data-tcm
  - data-placement
  - cache
  - c
confidence: source-reported
reproducibility: concept
related:
  - patterns-cmsis-dsp-helium-migration
  - hardware-dtcm-scatter-gather-banks
  - techniques-cortex-m85-memory-access-scheduling
  - techniques-armv8m-cache-management
sources:
  - https://developer.arm.com/community/arm-community-blogs/b/architectures-and-processors-blog/posts/armv8_2d00_m-based-processor-software-development-hints-and-tips
  - https://support.arm.com/documentation/107950/0100
aliases:
  - ITCM data contention
  - ITCM coefficients
  - instruction TCM bandwidth
  - literal data placement
---

When code executes from ITCM and repeatedly reads coefficients or literals from that same ITCM, data reads compete with instruction fetches for its bandwidth. The [Armv8.1-M software development hints and tips article](https://developer.arm.com/community/arm-community-blogs/b/architectures-and-processors-blog/posts/armv8_2d00_m-based-processor-software-development-hints-and-tips) reports 32 bits per cycle for M55 ITCM and 64 bits per cycle for M85 ITCM; the higher M85 figure does not remove possible fetch contention.

If the code must run from ITCM, place frequently used coefficient tables in another suitable memory with linker-script placement, then verify the linked address. The [Armv8.1-M software development hints and tips article](https://developer.arm.com/community/arm-community-blogs/b/architectures-and-processors-blog/posts/armv8_2d00_m-based-processor-software-development-hints-and-tips) also suggests declaring tables `static` instead of `const`, but a C storage-class declaration alone does not guarantee placement in DTCM or any particular physical memory; confirm the resulting section and address in the link output.

For literal-heavy control code, consider placing literal data in DTCM or executing from AXI-connected memory with caches enabled. Code executing from AXI-connected memory does not incur this specific ITCM instruction/data contention. These are placement choices for the described access pattern, not a claim that ITCM execution is generally slower.

## Cortex-M85 r1p0 ITCM access and DTCM placement

The Cortex-M85 r1p0 optimization guide says data accesses to the ITCM code region are supported. It recommends DTCM for data in performance-sensitive applications that are bound by TCM access, because ITCM serves instruction fetch and can manage one access per cycle, while DTCM is a dedicated four-way banked data memory. The implementation implication is that code fetches and data reads from ITCM can contend for that single access opportunity. Place critical data in DTCM when the workload and memory map permit, then verify the linked section and address. The Cortex-M85 r1p0 optimization guide does not say that ITCM data reads are unsupported or that every TCM workload benefits from moving data. [Arm Cortex-M85 Processor Software Optimization Guide, r1p0, Issue 01 (26 April 2023), §4.6, p. 45](https://support.arm.com/documentation/107950/0100)

The pipeline overview separately reports an instruction-fetch width of 64 bits per cycle from ITCM or I-cache. That fetch bandwidth figure is not a separate ITCM data port and does not contradict the one-access-per-cycle limit described for ITCM memory access. [Cortex-M85 r1p0 optimization guide, §2.2, p. 12; §4.6, p. 45](https://support.arm.com/documentation/107950/0100)
