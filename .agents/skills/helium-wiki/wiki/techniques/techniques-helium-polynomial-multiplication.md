---
id: techniques-helium-polynomial-multiplication
title: Map carryless polynomial products to Helium lanes
type: techniques
architectures:
  - armv8.1-m
tags:
  - helium
  - mve
  - polynomial-multiplication
  - c
  - asm
confidence: source-reported
reproducibility: concept
related:
  - techniques-helium-memory-access-layout
  - techniques-helium-multiword-carry-arithmetic
  - techniques-helium-instruction-interleaving
  - languages-arm-mve-intrinsics-and-assembly
sources:
  - https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf
aliases:
  - MVE carryless multiplication
  - Helium polynomial multiply lanes
  - VMULL P8 P16
  - MVE polynomial partial products
prerequisites:
  - Specify whether the result is a raw polynomial product or a separately reduced field element.
  - Define limb ordering, offset units, result width, and destination-lane behavior before assembling partial products.
---

Use these implementation rules when mapping carryless polynomial limbs to MVE multiply and lane operations. They describe raw polynomial products and partial-result handling; a complete field operation may require a separate reduction and system-level security properties must be established independently.

## Keep carryless multiply and XOR distinct from integer arithmetic

`VMULL` with `.P8` or `.P16` performs carryless polynomial products on selected bottom or top subelements and writes double-width results: `.P8` produces 16-bit results from 8-bit operands, and `.P16` produces 32-bit results from 16-bit operands. Combine polynomial partial products with XOR, which adds their binary polynomial coefficients without an integer carry. This raw product is neither ordinary integer multiplication nor, by itself, a reduction modulo a chosen polynomial; keep any field reduction as a separate, explicitly specified operation. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 80, 205–206](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

When aligning partial products across word boundaries, `VSHLC` can shift a complete vector as one value while transferring bits through its scalar carry input and output. Set that carry deliberately for each shift; independent per-lane shifts do not assemble a cross-limb result. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 68, 208, 211](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

## Match widening loads to the multiply subelements

For 16-bit polynomial limbs, a widening halfword load into 32-bit lanes places each limb in the low half of a word. A `.P16` bottom-half multiply can then consume those limbs and produce 32-bit partial products without requiring a matching top-half product for the same inputs. Confirm the loaded lane order and selected top/bottom form for the actual representation before XOR-assembling results. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 206, 209](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

## Form rotated limb views with bounded gathers

When a product needs rotated or permuted limb views, a gather using scaled offsets or a precomputed offset vector can avoid repeated scalar-to-vector lane shuffles. The choice depends on memory locality, offset construction cost, and register pressure. Verify whether the selected gather form interprets offsets in bytes or scaled elements, and include the offset vector in the live-register budget. See [Helium vector memory layout](techniques-helium-memory-access-layout.md). [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 210, 212](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

## Schedule partial products within the live-register budget

Keep only the partial products needed for the next XOR or lane assembly live at once, consuming intermediates before creating more where the dependencies allow. MVE has eight Q registers; compare the complete schedule's register liveness, reloads, recomputation, and spills rather than assuming a spill is beneficial. Reorder products only when their data dependencies and the selected processor's actual overlap rules permit it. Do not apply a Cortex-M55 overlap example to another MVE implementation. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 209–210](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

## Predicate partial-result lanes deliberately

Use lane predication when only selected middle limbs from a partial product belong in the accumulated result. Initialize or preserve excluded destination lanes according to the surrounding result contract; do not assume that an inactive lane is zero or harmless. Predicate setup can be scheduled away from dependent predicate-block use only when the target's predicate hazards and the dataflow allow it. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 211–212](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

## Combine independent loads and multiplies within target limits

Interleave independent gather loads and polynomial multiplies only where the actual microarchitecture permits overlap. Keep the instruction dependencies visible and reduce unnecessary use of callee-saved vector and scalar registers; follow the selected calling convention, stack alignment, and save/restore obligations for callable routines. Load/multiply alternation is a target-checked schedule opportunity, not a portable timing promise. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 210–212](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

When the same polynomial primitive runs repeatedly inside a larger routine, hoist constant offsets and predicate setup out of the inner loop when their values are invariant, and retain reusable values only while doing so fits the live-register budget. Integrating a helper can avoid repeated call and save overhead only if the enclosing function still meets the external ABI contract. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF p. 213](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

## Evaluate larger products by total assembly cost

For a wider carryless product, compare decomposition choices by the multiply work together with XORs, shifts, lane permutations, loads, and live-register pressure. Fewer polynomial multiplies alone do not establish a better implementation. Validate the raw product against an independent reference, then treat any modular reduction as a separate contract. Raw carryless multiplication does not by itself implement AES, ECC, or CRC, and it provides no constant-time or side-channel guarantee. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 205–213](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)
