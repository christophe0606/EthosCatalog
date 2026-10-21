---
id: patterns-sequential-buffer-overlays
title: Reuse data-buffer storage across nonoverlapping stages
type: patterns
architectures:
  - armv8.1-m
  - cortex-m52
  - cortex-m55
  - cortex-m85
tags:
  - data-placement
  - data-tcm
  - c
confidence: source-reported
reproducibility: concept
related:
  - patterns-helium-dtcm-dma-pipeline
  - techniques-cache-dma-buffer-visibility
sources:
  - https://github.com/Arm-Examples/Helium-Optimization/blob/bc9dfb67be64a2a5ee8e5325910a3f060a593863/Memory/Software_optimization_for_memory_constrained_devices.md#L56-L83
  - https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf
aliases:
  - sequential data-buffer overlay
  - reuse nonoverlapping DSP workspaces
prerequisites:
  - Establish each buffer's complete lifetime, including DMA, interrupt, and asynchronous consumers.
  - Use linker or allocator facilities only after verifying the selected toolchain's documented behavior.
---

Use sequential data-buffer overlays when two large temporary workspaces belong to stages whose live ranges never overlap. Assigning both workspaces to the same storage can reduce peak memory demand, but correctness depends on every reader and writer having completed before the next stage reuses that storage.

Build a lifetime table for each buffer, including initialization, last CPU use, pending DMA transfers, callbacks, interrupts, and asynchronous tasks. Overlay only buffers whose complete lifetimes are disjoint. Retain state that remains live across stages, and synchronize task execution or otherwise enforce nonconcurrency when the same address is shared. Storage reuse does not provide isolation: it can complicate debugging and can conflict with memory-protection boundaries between tasks.

The [Software optimization for DSP and ML memory-system article](https://github.com/Arm-Examples/Helium-Optimization/blob/bc9dfb67be64a2a5ee8e5325910a3f060a593863/Memory/Software_optimization_for_memory_constrained_devices.md) also mentions code overlays as possible. Keep that as a distinct design problem: replacing executable code requires separate execution, synchronization, instruction-cache coherency, and toolchain/linker contracts. This data-buffer pattern specifies no code-overlay mechanism. The Software optimization for DSP and ML memory-system article notes that small DSP or ML routines often fit well in cacheable memory, making data overlays more common. Analyze the workload before considering code overlays.

For DMA pipelines, include device ownership in the lifetime: a CPU cannot reuse a buffer while DMA still reads or writes it. See [TCM and DMA staging](patterns-helium-dtcm-dma-pipeline.md) and [CPU and DMA cache visibility](../techniques/techniques-cache-dma-buffer-visibility.md).

For NN deployment, include intermediate activations and each selected kernel's scratch in the lifetime map. Recurrent state can remain live across inference steps, skip connections can retain an earlier activation for a later layer, and asynchronous or DMA consumers can outlive the producing call. Reuse storage only after every such consumer has finished. If a pooling implementation writes in place, verify that its selected API permits input destruction and that no later reader needs the original tensor. Derive peak memory from the actual runtime, resolver, and release-specific kernel allocations rather than a fixed runtime allowance. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 219, 224, 229–230](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)
