---
id: patterns-helium-circular-buffer-addressing
title: Use wrapped Helium offsets for circular sample buffers
type: patterns
architectures:
  - armv8.1-m
tags:
  - helium
  - mve
  - circular-addressing
  - c
  - asm
confidence: source-reported
reproducibility: concept
related:
  - techniques-helium-memory-access-layout
  - techniques-helium-fir-tap-blocking
  - patterns-cmsis-dsp-helium-migration
sources:
  - https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf
  - https://developer.arm.com/community/arm-research/b/articles/posts/making-helium-going-around-in-circles
aliases:
  - Helium circular buffer
  - MVE wrapped gather offsets
  - VIWDUP circular addressing
  - vector ring buffer addressing
  - MVE contiguous access hint
---

Use wrapped vector offsets when a streaming kernel repeatedly consumes a window of recent samples and moving that full history after each sample would add unnecessary copying. This MVE pattern keeps the samples in a ring and generates logical offsets for gather or scatter accesses, subject to the instruction's documented step and boundary constraints.

## Replace per-sample history shifts with ring updates

For a streaming window, write each new sample into the slot holding the oldest sample, then advance the logical start position. A vector of wrapped offsets can present the window in the order required by the kernel without physically shifting every remaining history element on each input sample. Keep the physical slot order, logical sample order, and coefficient-to-sample mapping consistent. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 109–110](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

## Generate wrapped vector offsets within their preconditions

`VIWDUP` generates incrementing offsets with wrap, while `VDWDUP` generates decrementing offsets with wrap; both also update the scalar start value for the next vector. Use the generated offsets with the matching gather or scatter operation. The supported immediate step is one of 1, 2, 4, or 8. The start and wrap boundary must both be multiples of that step, and the start must be below the boundary; results can be unknown when these requirements are not met. Do not infer a power-of-two ring-size rule from traditional DSP ring buffers: follow the instruction constraints and the actual buffer contract. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF p. 96](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

Reuse the offset-vector register after its final gather/scatter consumer; regenerate it from scalar start and boundary state instead of keeping it live between accesses. [Making Helium: Going around in circles (3/4)](https://developer.arm.com/community/arm-research/b/articles/posts/making-helium-going-around-in-circles)

Keep offset units consistent with the access form. Unscaled gather/scatter offsets represent byte offsets; a shifted form scales offsets by the accessed element size. Choose a step, boundary, offset-vector element width, and access scale that express the intended addresses without relying on unsigned wrap to encode negative offsets. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 107–108](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

Portable C has no implicit circular-addressing construct, so a compiler may not generate this wrapped-offset pattern from ordinary pointer or array code. Where the compiler does not express the required behavior, use the matching ACLE intrinsics or a library routine with a documented contract; do not copy an incomplete sample signature. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 96, 111](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

## Treat contiguous-access hints as optional

Place wrapped-offset generation immediately before the intended gather when relying on an implementation's optional opportunity to recognize the wrapped access as one or two contiguous regions. An interrupt between offset generation and the load may discard that hint, but the gather must still return the correct values. The hint is implementation-dependent and must not be required for correctness or assumed to guarantee a timing result. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF p. 111](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

In the described implementation, modifying the offset vector clears its optional hint; exceptions may discard it. Ordinary gather behavior preserves correctness, without special metadata save/restore. [Making Helium: Going around in circles (3/4)](https://developer.arm.com/community/arm-research/b/articles/posts/making-helium-going-around-in-circles)

## Compare circular and block-linear FIR state

A circular FIR updates the oldest slot as each sample arrives, avoiding a per-sample copy of the rest of the history. The Arm Helium reference book's block FIR example instead keeps a linear state array sized for the tap history plus the current input block, and moves history once per block. That copy can be amortized over a larger block, while collecting a block can conflict with low-latency output requirements. Compare block size, state and copy traffic, output latency, and the selected library function's state contract. Circular addressing is an alternative design choice, not a mandatory CMSIS-DSP FIR migration; see [CMSIS-DSP Helium migration](patterns-cmsis-dsp-helium-migration.md) and [Helium FIR tap blocking](../techniques/techniques-helium-fir-tap-blocking.md). [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 110, 195–196](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)
