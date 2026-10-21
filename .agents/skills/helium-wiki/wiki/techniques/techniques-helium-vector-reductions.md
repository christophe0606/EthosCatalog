---
id: techniques-helium-vector-reductions
title: Select Helium reductions and accumulator formats
type: techniques
architectures:
  - armv8.1-m
tags:
  - helium
  - mve
  - vector-reduction
  - floating-point
  - c
  - asm
  - half-precision
confidence: source-reported
reproducibility: concept
related:
  - techniques-helium-fixed-point-arithmetic
  - techniques-helium-predication-and-tail-handling
  - languages-c-float16-half-precision
  - techniques-helium-auto-vectorization
  - techniques-helium-matrix-kernel-layout
  - patterns-helium-small-kernel-fusion
  - languages-arm-mve-intrinsics-and-assembly
  - patterns-cmsis-dsp-helium-migration
sources:
  - https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf
  - https://developer.arm.com/community/arm-research/b/articles/posts/making-helium-why-not-just-add-neon
  - https://support.arm.com/documentation/102095/0101/?lang=en
  - https://arm-software.github.io/acle/mve_intrinsics/mve.html
  - https://github.com/ARM-software/CMSIS-DSP/tree/main/Source/MatrixFunctions
  - https://github.com/ARM-software/CMSIS-DSP/tree/main/Source/SupportFunctions
  - https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Include/arm_helium_utils.h
  - https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/MatrixFunctions/arm_mat_vec_mult_f16.c
  - https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/StatisticsFunctions/arm_power_f32.c
aliases:
  - Helium complex Q31 dot product
  - Helium integer reductions
  - MVE dot product accumulation
  - Helium vector sum and extrema
  - MVE argmax and argmin
  - MVE floating-point reduction
  - MVE scalar accumulator for widening reductions
  - MVE s8 by s16 accumulator range
  - MVE F16 eight-lane pairwise reduction
  - Helium exponential reduction tail handling
  - post-gain complex energy reduction
  - sum of squares of gained I and Q components
---

Use these reduction choices when a Helium kernel combines lane results into a scalar sum, dot product, extremum, or index. Matching the reduction form to accumulator width, initialization, signedness, tail mask, and numeric order prevents scalar overflow and incorrect results from inactive lanes or changed floating-point association.

## Reduce integer sums and products into scalar accumulators

Use architectural integer across-vector sums or dot-product instructions when their operand widths and result semantics fit the kernel. These operations place the scalar result in general-purpose registers, reducing pressure on MVE's eight vector registers. `VADDV` returns an integer across-vector sum to a scalar register; `VADDLV` provides a 64-bit result for 32-bit input lanes. For dot products, `VMLAV` produces a 32-bit scalar result and `VMLALV` provides a long result for supported 16- or 32-bit inputs. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 59, 61, 85–86](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

The Making Helium: Why not just add Neon? article gives one design rationale: a widening vector result can cross an MVE beat boundary; supported scalar reductions avoid that result path and ease vector-register pressure. Select them by numeric semantics and range. [Making Helium: Why not just add Neon? (1/4)](https://developer.arm.com/community/arm-research/b/articles/posts/making-helium-why-not-just-add-neon)

Choose the accumulator width from a worst-case bound on the full sum or product accumulation, including input signedness, maximum product magnitude, number of terms, scaling, and any incoming accumulator. A non-accumulating form replaces the scalar result with the reduction of the current vector operands; the `A` form adds to the existing scalar accumulator. For a multi-vector reduction, initialize the accumulator to the correct identity and use the accumulating form for subsequent groups so earlier partial sums are retained. Use a wider operation when a 32-bit result cannot hold the proven range; handle a partial final vector so invalid lanes contribute nothing. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 61, 84–88](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

### Bound mixed signed-byte by signed-halfword products

For signed 8-bit weights in [-128, 127] and signed 16-bit inputs in [-32768, 32767], each exact product fits in signed 32 bits and has absolute value at most 2^22. A conservative sufficient condition for a signed 32-bit running sum with N products, incoming accumulator a0, and bias b is |a0| + N·2^22 + |b| ≤ 2^31−1. With zero initial value and no bias, N ≤ 511 guarantees that bound for every possible input combination. This is a mathematical worst-case bound, not a library limit; tighter documented input ranges can justify a less conservative bound.

ACLE provides vmladavaq_s16 for a signed 32-bit scalar accumulation and vmlaldavaq_s16 for a signed 64-bit scalar accumulation. If the row's proven range exceeds signed 32 bits, use a wider exact accumulation when that preserves the required output semantics, then prove the final conversion to s32 is in range or define its narrowing, wrap, rounding, and saturation behavior explicitly. Do not treat a signed 32-bit output type as proof that every intermediate sum fits; ordinary signed overflow in C is not a valid optimization contract. [Arm MVE Intrinsics Reference](https://arm-software.github.io/acle/mve_intrinsics/mve.html)

Initialize each row accumulator, sign-extend weights, and exclude inactive lanes in a partial final vector. A zero-column row has the defined empty-sum result; preserve that behavior. Long accumulators use register pairs, so include their GPR cost when choosing how many rows to process together; see [Helium matrix kernel layout](techniques-helium-matrix-kernel-layout.md) and [MVE ACLE and assembly contracts](../languages/languages-arm-mve-intrinsics-and-assembly.md).

For Q31 dot products, the rounding-high long form `VRMLALDAVH` has a distinct result contract: it accumulates into a 72-bit value, rounds, then returns the selected high 64 bits. Its optional scalar accumulator is shifted before being added into that wider representation. This is not an ordinary full-precision sum of products; retain the scaling and output format expected by the selected library function. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 86–87](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

### Reduce interleaved complex Q31 dot products

For a non-conjugating complex dot product with interleaved `[real, imaginary]` Q31 components, keep each pair together: for `a = ar + j ai` and `b = br + j bi`, accumulate `ar*br - ai*bi` into the real result and `ar*bi + ai*br` into the imaginary result. Four Q31 lanes in a 128-bit vector hold two complex samples, so preserve real/imaginary pairing instead of grouping arbitrary scalar lanes. Choose a function with these non-conjugating semantics; a conjugating dot product uses different signs. [Coding for Helium v1.1, §8, pp. 28–29](https://support.arm.com/documentation/102095/0101/?lang=en)

For the real component, the Coding for Helium v1.1 guide maps alternating product subtraction to `VRMLSLDAVHA.S32`, accumulating the rounding-high long result into a scalar register pair. This is a rounding-high long multiply/subtract/accumulate contract, not an ordinary full-precision scalar sum of products; select it only when the public function's scaling and rounding semantics match. [Coding for Helium v1.1, §8, p. 29](https://support.arm.com/documentation/102095/0101/?lang=en)

For the imaginary component, `VRMLALDAVHAX.S32` exchanges adjacent components and adds the paired products into a separate scalar register pair. Initialize both accumulators. Follow its add mnemonic and the stated formula rather than the Coding for Helium v1.1 guide's conflicting prose expansion that says subtract. [Coding for Helium v1.1, §8, pp. 29–30](https://support.arm.com/documentation/102095/0101/?lang=en)

The Coding for Helium v1.1 guide shifts both accumulated register pairs right by six to produce its example's Q16.48 result. Apply that shift only when the chosen function's result format requires it; confirm the public function's scaling, accumulation length, intermediate range, and rounding contract instead of assuming every complex Q31 reduction returns Q16.48. Bound the accumulated range for the full number of terms and check overflow against the selected accumulator representation. [Coding for Helium v1.1, §8, pp. 30–31](https://support.arm.com/documentation/102095/0101/?lang=en)

The public count in the Coding for Helium v1.1 guide is complex samples, while each sample occupies two scalar Q31 components and a vector holds two samples. Translate that count explicitly to the chosen loop's lane or tail-count convention, and ensure storage covers both components of every sample. Handle zero count without reading input and provide a bounded or predicated path for an odd final complex sample; verify tail accesses and accumulator range for the requested count. See [Helium lane predicates and tail handling](techniques-helium-predication-and-tail-handling.md). [Coding for Helium v1.1, §8, pp. 28–29, 31–32](https://support.arm.com/documentation/102095/0101/?lang=en)

## Preserve floating-point accumulation order and tail behavior

For floating-point dot products, accumulate products into lane-wise vector partial sums and then combine those lane sums in a final scalar reduction. Integer across-vector sum instructions do not provide the corresponding F16 or F32 reduction; use an explicit lane-combination sequence or a supported library helper. A pairwise F16 vector-reversal/addition tree can avoid extracting every lane individually, but it changes the order of additions. Validate the resulting rounding error against the application's tolerance; do not assume reordering leaves the final floating-point value unchanged. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 86, 138, 146–148](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

Use a predicate-aware reduction for an incomplete vector so inactive lanes do not enter the result. If a later operation reduces an intermediate vector, do not leave inactive lanes undefined and then include them in an unmasked reduction; use a supported predicate form or provide valid neutral values before reduction. Check both the arithmetic tail and any tail memory access, as described in [Helium lane predicates and tail handling](techniques-helium-predication-and-tail-handling.md). [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 145–148](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

## Reduce eight F16 lanes with an explicit pairwise tree

For eight F16 partial sums `a0` through `a7`, an MVE reduction can use two vector reversals and additions. `VREV32.16` swaps adjacent halfwords; adding the original and reversed vectors forms `(a0+a1)`, `(a2+a3)`, `(a4+a5)`, and `(a6+a7)` in replicated lane pairs. `VREV64.32` then exchanges the two 32-bit pairs within each 64-bit half; the next F16 addition forms each four-element partial sum in lanes 0–3 and 4–7. Add lanes 0 and 4 for the final result. The association is `((a0+a1)+(a2+a3)) + ((a4+a5)+(a6+a7))`, with F16 rounding at each addition.

The public [CMSIS-DSP v1.18.0 `vecAddAcrossF16Mve` helper](https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Include/arm_helium_utils.h) corroborates this lane tree by reinterpreting bits for the reversals, restoring the F16 view, and adding the selected scalar lanes. This is a pairwise tree, not a scalar left fold or a dedicated architectural F16 horizontal-add instruction. F16 VFMA lane accumulators already have their own association before this final tree, so validate rounding and intermediate range against the application contract.

Initialize lane accumulators to zero and include only valid elements in a partial block. Predicated updates should preserve accumulated values in inactive lanes. For a reduction of `exp(x-m)`, zero-filled input lanes are not neutral because `exp(0-m)` is generally nonzero: mask the contribution after the transform or predicate its accumulation, then reduce only valid contributions. Bound both sides of a dot-product tail and its arithmetic; multiplying an inactive zero by a non-finite padded value is not a safe identity. Use explicit predicates or scalar cleanup, and see [public F16 exponential contracts](../patterns/patterns-cmsis-dsp-helium-migration.md) for the vector and scalar paths.

## Reduce energy after applying a gain

For interleaved complex F32 samples `x_n = I_n + j Q_n` and real gain `g`, load each interleaved vector `u` (for example, `u = [I_0,Q_0,I_1,Q_1]`), compute the gained vector `v = g*u` once in the selected precision, then accumulate `VFMA(acc, v, v)` into zero-initialized lane sums. Across the valid scalar lanes, the result is `sum_n((g*I_n)^2 + (g*Q_n)^2)`: each real and imaginary component contributes once, and the result is total post-gain energy. Divide separately only if the API defines an average. This is the intended evaluation order; multiplying an original-energy result by `g*g` need not be bitwise equivalent because of intermediate rounding.

Accumulate lane partial sums through the valid input, then perform one horizontal reduction. Initialize before the first vector, return the defined zero result without reading input for an empty block, and ensure an inactive tail cannot enter the squared sum. Lane-wise F32 accumulation and its final reduction have a different association from a scalar sum; FMA and F16 intermediate range can also affect the result. Validate range, count, and allowed error for the application's precision. The public [CMSIS-DSP `arm_power_f32` implementation](https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/StatisticsFunctions/arm_power_f32.c) illustrates lane partial sums and a final helper reduction, but does not provide this fused gain-and-energy operation. See [small-kernel fusion](../patterns/patterns-helium-small-kernel-fusion.md) for retaining gained values during the store.

## Seed extrema and retain matching indices

Across-vector integer min/max operations compare the lane extremum with the incoming scalar value and update it only when the vector result is more extreme. Initialize with an identity that is valid for the selected signedness and operation, or seed from a valid input element. Absolute min/max variants compare absolute signed-lane magnitudes as unsigned values and treat the scalar accumulator as unsigned; do not use a signed seed contract for those forms. The Arm Helium reference book describes the floating-point number-min/max variants as following its cited IEEE 754 behavior for quiet NaNs, including returning the number when paired with a quiet NaN; confirm that behavior matches the application's required NaN contract. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 71–73](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

An argmax or argmin requires both the selected value and its matching index. Carry and update those lane vectors with the same comparison predicate, then reduce the values while preserving the selected index mapping. Define equal-value tie behavior explicitly: strict versus inclusive comparisons can select different matching indices. A final extrema reduction alone returns a value, not its index. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF p. 191](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

For a peak-bin search over a partial final vector, apply the valid-lane condition to both the value comparisons and the paired index selection, including the final selection that produces the reported bin. Invalid tail lanes must not supply either the winning value or its index. Preserve the tie rule across vector groups; a value-only maximum does not recover the index after the association has been lost. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF p. 191](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)
