---
id: techniques-helium-low-overhead-loop-alignment
title: Align Helium low-overhead-loop instructions to 32-bit boundaries
type: techniques
architectures:
  - armv8.1-m
  - cortex-m55
  - cortex-m85
tags:
  - loop-alignment
  - low-overhead-branch
  - helium
  - mve
  - asm
confidence: source-reported
reproducibility: snippet
related:
  - techniques-armv81m-feature-initialization
  - techniques-helium-auto-vectorization
  - techniques-helium-instruction-interleaving
sources:
  - https://developer.arm.com/community/arm-community-blogs/b/architectures-and-processors-blog/posts/armv8_2d00_m-based-processor-software-development-hints-and-tips
  - https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf
aliases:
  - Helium loop alignment
  - low-overhead loop alignment
  - MVE loop target alignment
  - WLS DLS alignment
---

For Helium code using the Armv8.1-M low-overhead loop instructions, align the loop's actual target to a 32-bit (four-byte) boundary. The [Armv8.1-M software development hints and tips article](https://developer.arm.com/community/arm-community-blogs/b/architectures-and-processors-blog/posts/armv8_2d00_m-based-processor-software-development-hints-and-tips) reports that an unaligned Helium instruction in a low-overhead loop can incur a one-cycle penalty per iteration; treat this as a possible, source-reported cost.

In handwritten or inline assembly, put `.p2align 2` immediately before the intended loop target:

```asm
.p2align 2
loop_target:
```

When inspecting compiler output, use the backward offset in `LE` to identify the loop target rather than assuming that the `WLS`/`DLS{TP}` instruction marks the loop body. A compiler-inserted `NOP` after `WLS`/`DLS{TP}` may be alignment padding outside the loop, so its location alone does not show that it executes on each iteration. Check the final assembly and target address.

The Arm Helium reference book likewise recommends aligning the low-overhead loop start when writing assembly. Apply the directive to the actual label reached by the backward loop branch, then inspect the final assembled address; padding before a nearby setup instruction is not evidence that the loop target itself is aligned. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF p. 155](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)
