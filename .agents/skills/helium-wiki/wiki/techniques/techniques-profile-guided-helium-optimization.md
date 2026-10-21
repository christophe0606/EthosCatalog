---
id: techniques-profile-guided-helium-optimization
title: Profile and prioritize Helium optimization work
type: techniques
architectures:
  - armv8.1-m
  - cortex-m52
  - cortex-m55
  - cortex-m85
tags:
  - helium
  - mve
  - profile-guided-optimization
confidence: source-reported
reproducibility: concept
related:
  - techniques-helium-auto-vectorization
  - patterns-cmsis-dsp-helium-migration
  - patterns-cmsis-nn-mve-runtime-selection
  - techniques-helium-trace-analysis
  - techniques-cortex-m-branch-prediction
  - techniques-helium-low-overhead-loop-alignment
  - techniques-helium-predication-and-tail-handling
  - techniques-helium-instruction-interleaving
  - techniques-helium-matrix-kernel-layout
  - techniques-cortex-m-data-prefetch-and-cache-hints
  - patterns-helium-dtcm-dma-pipeline
sources:
  - https://github.com/Arm-Examples/Helium-Optimization/blob/bc9dfb67be64a2a5ee8e5325910a3f060a593863/Performance_analysis/ml-dsp-front-end-optimization-with-helium.md#L88-L198
  - https://github.com/Arm-Examples/Helium-Optimization/blob/bc9dfb67be64a2a5ee8e5325910a3f060a593863/Performance_analysis/ml-dsp-front-end-optimization-with-helium.md#L868-L1018
  - https://github.com/Arm-Examples/Helium-Optimization/blob/bc9dfb67be64a2a5ee8e5325910a3f060a593863/Performance_analysis/ml-dsp-front-end-optimization-with-helium.md#L1286-L1336
  - https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf
  - https://documentation-service.arm.com/static/63190fc5ce77a54a32db3ac2
  - https://developer.arm.com/community/arm-research/b/articles/posts/making-helium-bringing-amdahl-s-law-to-heel
  - https://www.arm.com/products/development-tools/simulation/fast-models
  - https://developer.arm.com/community/arm-community-blogs/b/tools-software-ides-blog/posts/using-arm-models-to-compare-compiler-performance
aliases:
  - Helium hotspot profiling workflow
  - profile DSP and NN workloads
  - isolate hot loops for compiler inspection
  - scalar loop overhead after Helium vectorization
  - Amdahl's law and loop-control overhead
  - Fast Models versus Cycle Models for MVE evaluation
  - memory-bound dense kernel diagnosis
  - Cortex-M55 cache refill and demand-miss profiling
prerequisites:
  - Use a representative application build and workload.
  - Start with benchmarking and a supported profiling method on the intended physical board.
---

Start by benchmarking and profiling the representative application on the intended physical board. Prefer board measurements for performance decisions so that the actual processor, memory system, peripherals, and application configuration are exercised. Use profiling to select implementation work from the application's current bottlenecks. Compare end-to-end DSP, NN, and accelerator contributions instead of assuming that an earlier hotspot list remains valid after enabling Helium, changing libraries, or offloading work to an NPU.

## Iterate from application profile to kernel change

Capture a representative workload and identify which application stages currently account for the work of interest. Prefer a low-risk library substitution or data-placement change when it addresses the hotspot. After every structural change, profile the complete application again because the relative cost of DSP frontend, NN backend, and accelerator work can shift.

Active profiling can use markers, timestamps, PMU samples, or RTOS and stream-node transitions. It may require source instrumentation or a rebuild, and instrumentation can perturb execution. Start with the simplest supported board measurement that answers the question. If instruction-flow detail is needed, supported on-board ETM profiling can reduce source changes; its availability depends on the target, probe, and tool version.

Use Tarmac traces only as the very last resort when board benchmarking, ordinary profiling, generated-code inspection, and available board diagnostics leave a specific problem unexplained. Tarmac traces are generated in a simulated environment and produce very large logs, with substantial storage and post-processing costs. They can help investigate execution structure, but their timestamps do not establish performance on the board. Capture a minimal interval and confirm any resulting optimization with the complete workload on the board. See [board benchmarking and detailed trace analysis](techniques-helium-trace-analysis.md).

## Diagnose memory-bound dense kernels

Compare scalar and Helium row-group candidates with the same matrix, input, placement, and system configuration. Include both cold-start and representative steady-state or full-application behavior when those cases matter. Distinguish a repeatedly reused input from weights consumed once: warming the input does not imply that a large weight matrix is resident. Include any packing, staging, tails, output stores, and finalization in the workload being assessed.

Separate core instruction and load demand from weight-linefill delivery. When external-memory supply dominates, reducing arithmetic or repeated input loads may leave much of the wait unchanged; a smaller arithmetic loop is not proof that the memory bottleneck moved. A compact hot instruction body can coexist with costly data traffic, so verify instruction-side cache behavior rather than assuming it. Evaluate cache placement, hardware or software prefetch, and DMA staging under representative cache state and complete-application activity, including possible eviction of other useful data.

On Cortex-M55 r1p1, use available PMU events for cache access, refill, read-miss, and frontend or backend stall observations only according to the TRM event definitions. A refill event is not a literal count of external bus transfers and must not be substituted for a demand-read-miss event. Likewise, a prefetch request or detected stream does not prove that a line arrived before consumption. Select matching event scopes and corroborate them with the workload behavior. [Arm Cortex-M55 Processor Technical Reference Manual, r1p1, Issue 02, §17.2, pp. 266–267](https://documentation-service.arm.com/static/63190fc5ce77a54a32db3ac2)

A timing-capable model can help only when its configured scope represents the cache, memory latency and bandwidth, outstanding-request limits, DMA reachability, and relevant arbitration of the system under study. Otherwise, treat its result as incomplete evidence and use the target or a model with the required components. Recheck the complete application after each change, since a kernel optimization can alter both the memory pressure and other users’ cache residency.

## Reassess serial loop work after vectorization

After vectorizing a hot loop, the remaining serial work around the vector body can take a larger share of its cost. For short loops, inspect the generated loop for counter updates, conditional branches, setup, and scalar tail cleanup, then check whether those costs appear in the complete application profile. Branch cost depends on the target implementation, so verify it instead of assuming that scalar control always dominates. If profiling identifies counter or branch work as a bottleneck, consider a target-supported low-overhead loop form when its trip-count and control-flow semantics fit. Where supported and compatible with the loop's semantics, a tail-predicated form can avoid separate scalar cleanup; check the generated code and boundary behavior. [Making Helium: Bringing Amdahl's law to heel (4/4)](https://developer.arm.com/community/arm-research/b/articles/posts/making-helium-bringing-amdahl-s-law-to-heel)

Loop unrolling can amortize loop-control work across iterations, but it increases code size and may make vectorization more difficult. Compare the generated loop and representative application profile before adopting it; treat unrolling as a tradeoff to evaluate rather than a default. [Making Helium: Bringing Amdahl's law to heel (4/4)](https://developer.arm.com/community/arm-research/b/articles/posts/making-helium-bringing-amdahl-s-law-to-heel)

## Match the model to the question

A functional simulator can check instruction behavior and program correctness, but that result alone does not establish how a Cortex-M55 schedule, cache path, or packed row-block layout performs. Arm describes Fast Models as functionally accurate programmer's view models. Prefer benchmarking on the intended physical board when evaluating schedule and memory-system behavior. A timing-capable Cycle Model can provide supplementary evidence within its documented scope; verify which timing and memory features it actually implements. A model's timing is not proof of the complete target system's behavior. Use model results to form hypotheses and confirm performance conclusions by benchmarking on the board. [Arm Fast Models](https://www.arm.com/products/development-tools/simulation/fast-models) [Using Arm Models to Compare Compiler Performance](https://developer.arm.com/community/arm-community-blogs/b/tools-software-ides-blog/posts/using-arm-models-to-compare-compiler-performance)

Generated assembly and static scheduling analysis can help form a hypothesis. Evaluate the complete implementation with representative setup and placement, including any offline or online weight packing, entry/exit work, row and column tails, output stores, and fused finalization. Keep correctness checks on the functional path and use timing-capable evidence for performance questions; do not infer a measured kernel result from an instruction schedule alone. See [Helium matrix kernel layout](techniques-helium-matrix-kernel-layout.md).

## Isolate a hot loop for diagnosis

Function-level reports may hide loops inlined into callers. Use line or assembly execution profiling where available. A temporary noinline boundary can make a loop easier to attribute, inspect generated code for, and unit-test in isolation. Remove diagnostic attributes from production when they are no longer needed; LTO may make different cross-file inlining decisions in the production build.

Preserve the real input ranges, boundary cases, aliasing, and numerical behavior in focused tests. Use the isolated test to understand code generation, then confirm any change with the original complete workload. The [ML/DSP front-end optimization case study](https://github.com/Arm-Examples/Helium-Optimization/blob/bc9dfb67be64a2a5ee8e5325910a3f060a593863/Performance_analysis/ml-dsp-front-end-optimization-with-helium.md)'s platform contributions and profiler outputs are examples for its own build, not expected results for another system.

## Keep measurement conditions representative

Measure the complete system around the real workload, not just an isolated kernel. Record the selected core/device configuration, feature enablement, memory placement and cache state, and the RTOS and interrupt behavior that will be present in the application. Caches or other implementation options may be disabled until startup configures them, so use the same setup as the intended build when comparing candidate changes. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 165, 173–175](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

Bound profiling to the interval that answers the question. Semihosted `printf` or file I/O may involve debugger services and distort a short measurement; source instrumentation also adds work. Shorter periodic sampling can provide more detail but may increase sampling overhead. Separate setup, logging, and measurement so they do not become accidental parts of the interval under study. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 165–166, 173](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

## Check PMU support and event meaning

On Armv8.1-M PMU-enabled implementations, the PMU profiling interface extends counters also exposed through legacy DWT views; they are shared physical resources, not independent counter banks. Check the selected core/device's implemented PMU features and event support, and do not try to use PMU and legacy DWT profiling interfaces concurrently as if each owned separate counters. Use the processor's technical reference and applicable architecture documentation for its event set. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 166–170](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

Separate architectural retired-work events from optional microarchitectural observations. MVE instructions-retired and MVE load/store-retired events describe executed work; cache refills and frontend, backend, or MVE dependency stalls expose implementation-specific behavior and may be optional or conditional on the implemented cache and core. Confirm event availability before using a missing event as evidence that an event did not occur. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 170, 172](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

Combine counters only when their event scopes match the question: for example, compare cache refills with the corresponding cache-access events, contiguous MVE loads with all MVE loads, or MVE memory work with the intended MVE instruction work. Check the selected counters' widths and monitor their overflow flags. Resetting a count does not necessarily clear its overflow status, so clear overflow state separately using the target's documented interface. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 167–170, 172](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)
