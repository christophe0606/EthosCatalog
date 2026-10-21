---
id: techniques-helium-predication-and-tail-handling
title: Use Helium lane predicates and tail handling safely
type: techniques
architectures:
  - armv8.1-m
tags:
  - helium
  - mve
  - low-overhead-branch
  - lane-predication
  - tail-predication
  - c
  - asm
confidence: source-reported
reproducibility: concept
related:
  - techniques-helium-auto-vectorization
  - techniques-helium-low-overhead-loop-alignment
  - techniques-cortex-m-branch-prediction
  - techniques-armv81m-feature-initialization
  - techniques-helium-vector-data-alignment
sources:
  - https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf
  - https://support.arm.com/documentation/102095/0101/?lang=en
  - https://developer.arm.com/community/arm-research/b/articles/posts/making-helium-bringing-amdahl-s-law-to-heel
aliases:
  - MVE complex sample tail counts
  - Helium lane predication
  - MVE tail predication
  - WLSTP element count units
  - WLSTP.8 byte count
  - MVE tail-predicated counts
  - VCTP tail handling
  - VPT and VPST lane conditions
  - MVE predicate masks
---

Use these contracts when writing or reviewing Helium vector loops with per-lane conditions or incomplete final vectors. They prevent zero-count entry errors, mismatched predicate widths, unintended inactive-lane values, out-of-bounds accesses, and stale tail state after early exits.

## Match loop form to trip-count semantics

Use `WLS` or `WLSTP` for a loop that may have zero iterations: they branch around the body when the count is zero. Use `DLS` or `DLSTP` only for do-loop behavior whose contract guarantees at least one iteration. Without tail predication, `LR` represents the number of loop iterations. With `WLSTP` or `DLSTP`, `LR` represents the number of remaining elements, not the number of vector iterations, and `LETP` decrements it by the vector lane capacity selected for the element size. The final iteration predicates lanes beyond the remaining element count. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 50, 53, 117](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf) [Making Helium: Bringing Amdahl's law to heel (4/4)](https://developer.arm.com/community/arm-research/b/articles/posts/making-helium-bringing-amdahl-s-law-to-heel)

The `WLSTP` size suffix selects the element width, and the initial count uses that element's units: `WLSTP.8` counts remaining bytes, while wider forms count remaining elements of their selected width. A byte-oriented tail loop therefore needs a byte count in `LR`. The Making Helium: Bringing Amdahl's law to heel article's copy illustration assumes a non-overlapping ordinary-memory copy; it does not establish overlap-safe or memory-mapped-I/O behavior. Preserve `WLSTP`'s zero-count branch before every input and output access, keep active lanes within both buffer bounds, and preserve the function return address and calling-convention requirements before reusing `LR` as the loop counter. [Making Helium: Bringing Amdahl's law to heel (4/4)](https://developer.arm.com/community/arm-research/b/articles/posts/making-helium-bringing-amdahl-s-law-to-heel)

Tail predication keeps a partial final vector in the vector loop; the unprocessed lanes are disabled according to the remaining element count. For explicit intrinsic tails, create the predicate with `VCTP` using an element width that matches the consuming load, arithmetic, or store. This avoids a separate scalar cleanup only when the tail operations themselves honor that predicate. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 100, 150](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf) [Making Helium: Bringing Amdahl's law to heel (4/4)](https://developer.arm.com/community/arm-research/b/articles/posts/making-helium-bringing-amdahl-s-law-to-heel)

### Count complex samples in scalar component lanes

In the Coding for Helium v1.1 guide's complex Q31 example, `numSamples` counts complex samples, each stored as two scalar 32-bit components, real and imaginary. `WLSTP.32` therefore receives `2 * numSamples` scalar components; a full 128-bit vector has four 32-bit lanes and holds two complete complex samples. [Coding for Helium v1.1, §8, pp. 28–29, 32](https://support.arm.com/documentation/102095/0101/?lang=en)

For `WLSTP.32`, `LR` tracks remaining scalar components and `LETP` advances by the selected lane capacity, not one complex sample per iteration. An odd number of complex samples leaves a two-component real/imaginary tail; preserve each pair and do not present an arbitrary one-component tail as a valid complex sample. [Coding for Helium v1.1, §8, pp. 29, 31](https://support.arm.com/documentation/102095/0101/?lang=en)

Preserve WLSTP's zero-count branch so the loop body performs no input load. Before doubling, check that `2 * numSamples` is representable in the loop count type; otherwise unsigned overflow can shorten the loop. Apply tail predication to memory operations and reduction participation, and keep every active component within the caller's buffers; masking arithmetic after an unpredicated load does not make an out-of-bounds read safe. [Coding for Helium v1.1, §8, pp. 29, 31–32](https://support.arm.com/documentation/102095/0101/?lang=en)

## Preserve predicate granularity and inactive-lane meaning

MVE predicate bits describe bytes: a 128-bit vector has 16 predicate bits, and every bit belonging to one wider lane must agree. A byte-wise comparison or hand-built mask can therefore be unsuitable for halfword or word operations when adjacent byte bits differ. Produce the mask at a width compatible with the operation that consumes it; do not reuse a byte mask blindly for wider lanes. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 98, 144–145](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

Use vector comparison with `VPT` or `VPST` for per-lane control, including then/else conditions and nested tests. `VPT` forms a comparison-based block; `VPST` applies an existing predicate. A comparison inside a vector predication block can update the lane conditions for a nested test. Scalar Thumb `IT` blocks generally cannot contain MVE instructions, so they are not a substitute for lane predication. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 97–101, 103](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

Choose inactive-lane behavior deliberately. A merging operation retains its supplied inactive value; a zeroing load supplies zeros to inactive lanes; predicated reductions and stores exclude inactive lanes; and a don't-care result has no defined inactive value and must not be observed later. When a merge parameter is required, give it the same lane width as the result. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 143–145, 150](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

## Keep memory and scalar effects within their contracts

Predicate the memory access itself on a partial vector, not only arithmetic performed after the load. An unpredicated full-width load can still read past the caller's object even if a later vector operation masks unused lanes. The Arm Helium reference book's dot-product remainder example uses full-width loads; do not copy that pattern unless a separate documented allocation contract makes every loaded lane valid. Use a predicated load or a correct scalar cleanup, and ensure stores leave inactive destination elements untouched. Keep allocation bounds, padding, and alignment as separate conditions; see [Helium vector data alignment](techniques-helium-vector-data-alignment.md). [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 145, 147–150](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

Lane predication does not generally suppress scalar side effects of a vector instruction. For example, base-register writeback can still advance an address even when every lane is inactive. Scalar-result reductions also need deliberate initialization and accumulation because a scalar result can be overwritten even when lane predicates are used; then/else predication of scalar-result variants is not a scalar select. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF p. 102](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

If control flow exits a tail-predicated loop early, execute `LCTP` before later vector operations so loop mode and active tail predication are cleared. Otherwise, the old tail state can mask lanes in subsequent vector work. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF p. 118](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

Low-overhead branch is part of Armv8.1-M whether or not MVE is implemented; tail predication is the MVE-related loop form. Low-overhead loops remain interruptible: an interrupt or other exit can flush cached loop information, and executing the loop-end instruction after return rebuilds it. Do not disable interrupts merely to use these loops. Keep loop-alignment advice scoped to the actual implementation and see [Helium low-overhead-loop alignment](techniques-helium-low-overhead-loop-alignment.md). [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 50–51, 117](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)
