---
id: techniques-helium-multiword-carry-arithmetic
title: Propagate carry across Helium multiword arithmetic
type: techniques
architectures:
  - armv8.1-m
tags:
  - helium
  - mve
  - multiword-carry-arithmetic
  - c
  - asm
confidence: source-reported
reproducibility: concept
related:
  - techniques-helium-predication-and-tail-handling
  - languages-arm-mve-intrinsics-and-assembly
sources:
  - https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf
aliases:
  - MVE multiword addition
  - Helium vector carry propagation
  - VADC VSBC multi-limb arithmetic
  - FPSCR carry chain
prerequisites:
  - Define limb order, the initial carry or borrow state, and the externally visible carry-out contract.
  - Keep every loaded limb within the input allocation, including an incomplete final vector.
---

Use this guidance when implementing multi-limb integer arithmetic with MVE. `VADC` and `VSBC` carry state between 32-bit limbs, so limb order, flag initialization, and final partial-vector handling are part of the arithmetic result.

## Order limbs from least significant to most significant

Represent the integer as ordered unsigned 32-bit limbs and process the least-significant limb first. `VADC` or `VSBC` propagates carry between the active lanes of a vector; the final carry state feeds the next higher vector chunk. Ordinary lane-wise `VADD` or `VSUB` computes independent results and does not implement this cross-limb carry chain. Preserve the low-to-high ordering when loading, storing, or dividing the number into chunks. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 60–61](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

## Initialize and preserve the correct carry state

The carry input and output for `VADC` and `VSBC` use `FPSCR.C`, which is distinct from the scalar integer `APSR.C`. Establish the initial vector carry explicitly for each independent chain; the immediate-initialization variants can force the carry state when the operation requires a known initial value. For subtraction, interpret carry as no-borrow state: an incoming carry of one means no borrow into that limb, while zero means a borrow is propagated. Retain the vector carry from each lower chunk for the next chunk, and define how the routine establishes or returns carry state at its interface rather than relying on scalar `APSR.C` or assuming `FPSCR.C` survives an unrelated call. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF p. 60](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

## Give the final partial limb vector arithmetic tail semantics

For a final vector with fewer valid limbs than lanes, mask the arithmetic chain itself or handle the remainder with a scalar carry loop. Zero-loading inactive limbs and masking the stores is not sufficient for an unpredicated carry operation: an extra zero limb can consume the carry that should leave the highest real limb. With predicated carry operations, verify that inactive beats do not update the logical carry-out; also keep both input loads and output stores within their allocated ranges. See [Helium lane predicates and tail handling](techniques-helium-predication-and-tail-handling.md). [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 60, 205](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

## Use whole-vector carry shifts when limbs must shift together

`VSHLC` shifts the complete vector as one 128-bit value, taking carry-in from and returning carry-out to a scalar register. Use it when a multiword quantity must shift across its internal 32-bit boundaries or when vector limbs are assembled with a scalar carry. This differs from an element-wise vector shift, which shifts each lane independently and does not transfer bits between limbs. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF p. 68](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)
