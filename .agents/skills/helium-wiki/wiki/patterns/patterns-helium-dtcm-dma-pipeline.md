---
id: patterns-helium-dtcm-dma-pipeline
title: Stage DSP data through TCM and DMA buffers
type: patterns
architectures:
  - armv8.1-m
  - cortex-m52
  - cortex-m55
  - cortex-m85
tags:
  - data-placement
  - data-tcm
  - instruction-tcm
  - helium
confidence: source-reported
reproducibility: concept
related:
  - hardware-itcm-instruction-data-contention
  - hardware-dtcm-scatter-gather-banks
  - techniques-cortex-m-data-prefetch-and-cache-hints
  - techniques-cache-dma-buffer-visibility
  - patterns-sequential-buffer-overlays
  - techniques-helium-matrix-kernel-layout
  - techniques-profile-guided-helium-optimization
sources:
  - https://github.com/Arm-Examples/Helium-Optimization/blob/bc9dfb67be64a2a5ee8e5325910a3f060a593863/Memory/Software_optimization_for_memory_constrained_devices.md#L24-L52
  - https://github.com/Arm-Examples/Helium-Optimization/blob/bc9dfb67be64a2a5ee8e5325910a3f060a593863/Memory/Software_optimization_for_memory_constrained_devices.md#L56-L83
  - https://github.com/Arm-Examples/Helium-Optimization/blob/bc9dfb67be64a2a5ee8e5325910a3f060a593863/Memory/Software_optimization_for_memory_constrained_devices.md#L105-L115
  - https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf
aliases:
  - DTCM working-set allocation
  - DMA ping-pong DSP buffers
  - ITCM and DTCM real-time placement
  - dense matrix weight DMA row-group staging
  - DTCM ping-pong weight blocks
prerequisites:
  - Check the processor and system memory map, TCM capacity, linker placement, cache attributes, and DMA reachability.
  - Define buffer ownership and synchronization between DMA and CPU before enabling overlap.
---

Use this pattern when a Cortex-M DSP or ML workload has a small time-critical working set, larger streaming buffers, or DMA transfers that can overlap CPU processing. TCM availability, capacity, access paths, retention, security partitions, and reachability vary by processor and system integration; verify them for the device before assigning memory.

## Prioritize deterministic code and data

Plan placement around the active workload rather than total program size. When bounded interrupt response matters, first evaluate the vector table and critical exception or interrupt handlers for ITCM placement; evaluate the main stack and critical working data for DTCM placement, subject to the device's startup, linker, and memory-map requirements. Then allocate remaining TCM to the hot DSP or ML code and data blocks that benefit from deterministic access. Keep instruction and data TCM roles distinct: constants placed with code can compete with instruction fetches, and some cores have narrower ITCM data paths. Check the selected processor's documented ports, bus widths, and bank behavior rather than assuming the TCMs are interchangeable.

A cacheable SRAM working set may be sufficient for sequential streams, while DTCM can be reserved for data whose latency or predictability matters. Compare the actual linked placement and steady-state hot set; a large image does not imply that every instruction or buffer needs TCM. Link [core-specific prefetch guidance](../techniques/techniques-cortex-m-data-prefetch-and-cache-hints.md) and [ITCM instruction/data contention](../hardware/hardware-itcm-instruction-data-contention.md).

## Stage input with explicit DMA ownership

For streaming input, larger I/O buffers can remain in system SRAM while the current processing block is staged in DTCM. This can reduce DTCM usage while increasing total storage, because the system-SRAM I/O buffers and staged working buffers coexist. A ping-pong arrangement can let DMA fill one input buffer while the CPU consumes the other, but use two buffers only when the application needs this overlap and the system supports it. Define the ownership sequence explicitly: DMA owns and fills a buffer; a completion event and required synchronization transfer ownership to the CPU; the CPU consumes the completed data; only after consumption may the buffer return to DMA.

### Stage dense weights by row group

As an application of this pipeline to a dense matrix whose weights are streamed from external memory, stage a row or packed row-group block in an allocated DTCM buffer, let the CPU consume a completed block, and transfer a future block while that work proceeds only when the DMA path and workload can usefully overlap. Keep a repeatedly used input vector in DTCM as well when it fits alongside stacks, state, output buffers, and other critical data. DTCM capacity, bank use, and shared interconnect traffic constrain the block size; two weight buffers are justified only when the overlap they enable is useful.

DMA changes when and where weight transfers occur; it does not remove the weight bytes or their memory latency. If transfer delivery cannot keep pace with the kernel, block transitions still wait and processing from cacheable main memory may be preferable. Keep each staged block associated with the correct output rows and biases, handle incomplete row and column blocks within allocated bounds, and do not let the CPU observe a block before transfer completion or let DMA reuse it before consumption. Apply the existing cache visibility and ownership rules to any cacheable DMA source or destination.

Confirm that the selected DMA can access the chosen TCM region and that bus arbitration, interconnect contention, transfer duration, and processing deadlines permit useful overlap. DMA activity does not by itself accelerate the kernel. If those conditions are not met, a single staged buffer or system-SRAM processing path may be simpler.

## Choose output placement separately

CPU-produced output may stay in main memory when buffering or the memory system can absorb stores. DTCM output followed by a DMA transfer is another option when the platform supports it and the transfer can be scheduled safely. Overlapping output transfer with input transfer is conditional on independent DMA channels and interconnect resources, and can require more buffers. Treat cache visibility and ownership as a separate contract; see [CPU and DMA cache visibility](../techniques/techniques-cache-dma-buffer-visibility.md).

## Confirm TCM mapping and DMA reachability

TCM is explicitly mapped storage, not an automatically populated cache. Its presence, capacity, instruction/data roles, ports, and direct DMA access depend on the processor and system integration; a DMA controller can reach TCM only when that path is implemented. Before placing a hot working set, verify the memory map, linker placement, available capacity, TCM role and access path, and DMA reachability for the actual device. Predictable local TCM access does not establish a system-wide hard real-time guarantee. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 174, 176](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)
