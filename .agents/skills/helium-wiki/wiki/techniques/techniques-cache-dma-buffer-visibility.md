---
id: techniques-cache-dma-buffer-visibility
title: Make CPU-written buffers visible to DMA and other bus masters
type: techniques
architectures:
  - armv8.1-m
  - cortex-m52
  - cortex-m55
  - cortex-m85
tags:
  - cache
  - cache-management
  - data-placement
confidence: source-reported
reproducibility: concept
related:
  - techniques-armv8m-cache-management
  - patterns-helium-dtcm-dma-pipeline
  - patterns-sequential-buffer-overlays
sources:
  - https://github.com/Arm-Examples/Helium-Optimization/blob/bc9dfb67be64a2a5ee8e5325910a3f060a593863/Memory/Software_optimization_for_memory_constrained_devices.md#L84-L104
aliases:
  - Cortex-M cache clean before DMA
  - cache policy for CPU and DMA buffers
prerequisites:
  - Check the selected core and system documentation for cache maintenance, memory attributes, and synchronization requirements.
  - Define CPU and DMA ownership transitions for each shared buffer.
---

Use this guidance for cacheable buffers that the CPU writes and a DMA engine or another bus master later reads. A dirty cache line can contain newer CPU output than the copy in memory. Transfer ownership only after making the required data visible to the external consumer and completing the platform's ordering and synchronization steps.

## Choose policy by buffer use

Write-back caching can reduce main-memory traffic when the CPU reuses intermediate data locally, so CPU-only intermediate buffers generally do not need cleaning between CPU stages. At a CPU-to-DMA ownership boundary, however, dirty output lines must be cleaned to the point observed by the DMA before the DMA reads the buffer. Follow the core and system requirements for maintenance completion, barriers, and ownership handoff.

A write-through region policy can avoid the specific dirty-output condition because stores are propagated toward memory, though it can increase memory traffic and does not eliminate the need to obey synchronization and ordering rules. A noncacheable or appropriately shareable mapping may also be suitable for buffers that are frequently exchanged with a device. The exact MPU attributes and their effects depend on the processor, interconnect, and any outer or system cache. A nonshareable mapping is not the same as a noncacheable mapping.

Choose write-back, write-through, or noncacheable policy per buffer role and actual memory hierarchy. Keep CPU-only working data cacheable when reuse benefits from it, and handle maintenance at ownership boundaries for shared output.

## Scope of this guidance

The [Software optimization for DSP and ML memory-system article](https://github.com/Arm-Examples/Helium-Optimization/blob/bc9dfb67be64a2a5ee8e5325910a3f060a593863/Memory/Software_optimization_for_memory_constrained_devices.md) discusses CPU-written output becoming visible to an external bus manager. It does not define a complete DMA-to-CPU invalidate procedure, register sequence, or universal write-through configuration. For device-written buffers, use the selected platform's documented cache maintenance and synchronization procedure; do not infer it from the CPU-write case.