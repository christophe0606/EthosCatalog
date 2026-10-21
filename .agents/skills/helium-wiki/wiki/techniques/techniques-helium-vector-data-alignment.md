---
id: techniques-helium-vector-data-alignment
title: Align vector data for the target memory path
type: techniques
architectures:
  - cortex-m55
  - cortex-m85
tags:
  - buffer-alignment
  - data-placement
  - helium
  - mve
confidence: source-reported
reproducibility: concept
related:
  - patterns-cmsis-dsp-helium-migration
  - techniques-cortex-m-memory-access-scheduling
  - techniques-cortex-m85-memory-access-scheduling
  - hardware-dtcm-scatter-gather-banks
sources:
  - https://github.com/Arm-Examples/Helium-Optimization/blob/bc9dfb67be64a2a5ee8e5325910a3f060a593863/General_optimizations/processor_specific.md#L19-L28
  - https://github.com/Arm-Examples/Helium-Optimization/blob/bc9dfb67be64a2a5ee8e5325910a3f060a593863/General_optimizations/processor_specific.md#L46-L57
  - https://support.arm.com/documentation/107730/0003
aliases:
  - MVE buffer alignment
  - Cortex-M vector data layout
  - M55 relative DTCM buffer offsets
  - MVE source and destination offset experiment
prerequisites:
  - Check the selected instruction, library API, ABI, memory region, and processor documentation for actual alignment requirements.
---

Use this guidance when arranging vector buffers for MVE loads and stores on Cortex-M55 or Cortex-M85 systems. Required alignment depends on the specific instruction, library API, compiler contract, and target; the [Processor specific details article](https://github.com/Arm-Examples/Helium-Optimization/blob/bc9dfb67be64a2a5ee8e5325910a3f060a593863/General_optimizations/processor_specific.md)'s broad word-alignment statement should not be treated as a universal fault rule for every Helium access.

## Correctness requirements versus performance alignment

Separate correctness requirements from performance choices. Satisfy the documented alignment required by the selected access and library contract first. Then, where data is in uncached main memory, compare the actual transfer width and starting address: a vector access that crosses the relevant bus transfer boundary may require additional transfers. For cacheable memory, consider whether the access spans cache lines and whether vector-size alignment reduces split-line accesses. These effects depend on the core, memory path, cache configuration, access width, and actual buffer address.

## Keep alignment, allocation, and tails distinct

Alignment does not make an out-of-range vector access valid. Keep allocation size, padding, and tail handling separate: every lane must be masked, cleaned up, or otherwise kept within the object bounds according to the API contract. Check linker placement and runtime addresses when alignment is intended to affect a physical memory path. See the [CMSIS-DSP migration and buffer contracts](../patterns/patterns-cmsis-dsp-helium-migration.md) and the [Cortex-M memory-access scheduling guidance](techniques-cortex-m-memory-access-scheduling.md) for core-specific access details.

## Compare relative source and destination offsets on M55 DTCM paths

For a confirmed Cortex-M55 DTCM store-to-read conflict in an independent source/destination kernel, compare the buffers' actual offsets as a whole-kernel experiment. One candidate arrangement is a source address divisible by 16 and a destination address with low four address bits equal to 8. This is a conditional placement hypothesis, not a universal vector-alignment rule: confirm the memory path and access sequence, inspect linker or runtime addresses, and evaluate the complete kernel. The processor-specific guidance describes DTCM read-after-write behavior; it does not make this offset pair effective for every access pattern. [Cortex-M55 processor-specific optimization guidance](https://github.com/Arm-Examples/Helium-Optimization/blob/bc9dfb67be64a2a5ee8e5325910a3f060a593863/General_optimizations/processor_specific.md#L46-L57)

An address ending in `0x8` modulo 16 is eight-byte aligned, not sixteen-byte aligned. A selected library or instruction contract that requires sixteen-byte alignment takes precedence over this experiment. Keep object bounds and tails valid, and do not confuse this store/read placement question with gather-load bank conflicts covered by [TCM scatter/gather bank guidance](../hardware/hardware-dtcm-scatter-gather-banks.md). Exact in-place buffers cannot be independently offset; use independent-work scheduling where applicable.
