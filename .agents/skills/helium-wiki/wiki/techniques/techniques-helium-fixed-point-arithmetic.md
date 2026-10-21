---
id: techniques-helium-fixed-point-arithmetic
title: Match Helium fixed-point widths, rounding and saturation
type: techniques
architectures:
  - armv8.1-m
tags:
  - helium
  - mve
  - fixed-point-arithmetic
  - c
  - asm
confidence: source-reported
reproducibility: concept
related:
  - languages-c-float16-half-precision
  - techniques-helium-auto-vectorization
  - techniques-helium-vector-reductions
sources:
  - https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf
  - https://developer.arm.com/architectures/system-architectures/software-standards/acle
aliases:
  - Helium Q15 and Q31 arithmetic
  - MVE fixed-point scaling
  - MVE saturating multiply and narrow
  - Helium rounding shifts
  - MVE top and bottom lane mapping
  - fixed-point normalization with CLZ
prerequisites:
  - Know the input and output Q formats and numeric range of each operation.
  - Specify the required signedness, overflow, rounding, and saturation behavior.
---

Use these rules when mapping fixed-point C arithmetic to Helium vector operations. They help preserve the scalar numeric contract while avoiding overflow in products and accumulators, incorrect scaling, and stale lanes during widening or narrowing.

## Choose element widths from ranges and intermediate growth

Q15 and Q31 encode fractional values with fixed binary-point positions, so derive the product and accumulator ranges from the chosen formats. A Q15-by-Q15 product is wider than either input; converting its scale to Q31 requires doubling the product. The endpoint where both operands represent negative one produces a positive result that exceeds the signed Q31 maximum after doubling, so saturate it to the greatest representable positive value. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 32, 35](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

When an addition needs headroom, a halving add uses an extended intermediate and then halves the result. It computes half of the extended sum, while saturating addition keeps the sum's scale and clips values to the destination range; both may store in the same Q format but produce different values. Choose between them from the algorithm's format contract; they are not interchangeable ways to handle overflow. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF p. 36](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

Use long or widening operations, or widen operands before multiply-accumulate, when the full intermediate must be retained. Keep that headroom through accumulation and apply the required rescaling before narrowing. Low-half and high-half products retain different portions of a product; doubling changes its scale, rounding changes discarded-bit handling, and saturation clips out-of-range results. Select each operation family to match the scalar reference, rather than treating these modifiers as interchangeable performance options. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 55–56, 78–81](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

The grayscale illustration in the Arm Helium reference book shows how to prove an intermediate width for one expression: byte inputs are widened before multiply-accumulate, and the maximum sum for its coefficients is `255 × (77 + 151 + 28) = 65,280`, which fits an unsigned 16-bit accumulator. The coefficients and expression belong only to that example. For another kernel, calculate the bound from its own maximum input values, coefficient magnitudes, and accumulation length before choosing an intermediate type. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 199–200, 202](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

## Preserve rounding and saturation behavior

Combine shifting, rounding, saturation, and narrowing only when the selected instruction provides the needed combination and matches the required result. High-half, doubling-high-half, rounding, saturating, and saturating-narrow operations have distinct scaling and overflow behavior. Signed-to-unsigned saturation also has a boundary contract for negative inputs. Check negative values, half-way rounding cases, the most-negative input, and values just beyond each destination endpoint against the scalar definition. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 55–56, 65–69, 94–95](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

A rounding right shift follows its instruction-defined rounding rule; the Arm Helium reference book describes its vector rounding shift as adding a half-unit before truncation. For signed values this is not automatically equivalent to C integer division, truncation toward zero, or ties-to-even rounding. Validate the sign and tie cases that matter to the application before replacing a scalar expression. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 56, 65–67, 122–125](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

For a quantized NN operator, use vector requantization, rounding shifts, or saturating high-half multiplies only when their rounding, tie, overflow, and saturation behavior matches that operator's selected API. Fusing operations can change results. The legacy `bias_shift`/`out_shift` Q-format relationship applies only to a matching legacy API; do not substitute it for TFLM's zero-point, per-channel-scale, and requantization contract. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 220, 222–223, 228–229](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

## Track source and destination lane shapes

For long or widening operations, top and bottom forms select alternating source elements: bottom selects even-numbered elements and top selects odd-numbered elements. They do not select two contiguous halves of the input lanes. Narrowing forms place results into alternating destination lanes; the other destination lanes retain their previous values. Initialize or overwrite every lane that the program can later observe. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 56–57, 79, 94–95](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

F16-to-F32 conversion has its own top/bottom mapping. In the Arm Helium reference book's bottom-conversion example, F16 lanes 0, 2, 4, and 6 become the four F32 values; the top form handles the complementary elements. Do not assume that “bottom” means a contiguous run of the first half of the F16 lanes. Check the selected ACLE conversion definition and preserve the lane order when storing or combining converted values. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 74–75; Arm C Language Extensions (ACLE)](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

Numeric conversion and bit reinterpretation also have different contracts: `vcvt` converts values between numeric formats and can round or lose representable precision, while `vreinterpret` preserves the bits and changes only their type interpretation. Use a conversion when the numeric value must be preserved across formats; use reinterpretation only when the bit-level representation is intended. [Arm C Language Extensions (ACLE)]

For float-to-integer conversion, select a supported rounding mode that matches the scalar reference contract. Check positive and negative values, half-way cases, and the destination endpoints; the conversion's rounding behavior is part of its numeric result, not a type-only change. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF p. 74; Arm C Language Extensions (ACLE)](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

## Normalize vectors with a valid reduction seed

For a shared safe normalization shift, count leading zeros for unsigned lanes or leading sign bits for signed lanes, then reduce the counts to the minimum so the shift is safe for every lane. Seed a minimum reduction with the largest valid count for its element width; the Arm Helium reference book's 32-bit leading-zero example uses 32. A zero lane has a full-width leading-zero count. If every lane is zero, the reduction remains at that full-width seed, so handle the all-zero vector before applying a shift at the width boundary. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF p. 76](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

## Use scalar shifts for scalar work and tails

Armv8.1-M also provides scalar general-purpose-register shifts, including long, rounding, and saturating forms. These can be useful for reduced scalar work or a tail after a vector loop. Choose the scalar form from the required width, signedness, direction, rounding, and saturation behavior. Its semantics and timing are scalar instruction properties; do not infer them from vector shift behavior. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 122–125](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)
