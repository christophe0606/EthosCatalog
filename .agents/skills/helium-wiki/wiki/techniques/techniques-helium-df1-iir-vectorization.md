---
id: techniques-helium-df1-iir-vectorization
title: Vectorize DF1 IIR recurrences with Helium coefficient layouts
type: techniques
architectures:
  - armv8.1-m
  - cortex-m55
tags:
  - helium
  - mve
  - floating-point
  - biquad-recurrence-vectorization
  - iir-recurrence-vectorization
  - instruction-scheduling
  - c
  - asm
confidence: inferred
reproducibility: concept
related:
  - patterns-cmsis-dsp-helium-migration
  - techniques-helium-df2t-lane-mapping
  - techniques-helium-instruction-interleaving
  - languages-c-float16-half-precision
sources:
  - https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/FilteringFunctions/arm_biquad_cascade_df1_init_f32.c
  - https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/FilteringFunctions/arm_biquad_cascade_df1_init_f16.c
  - https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/FilteringFunctions/arm_biquad_cascade_df1_f32.c
  - https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/FilteringFunctions/arm_biquad_cascade_df1_f16.c
  - https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/FilteringFunctions/arm_biquad_cascade_df1_q31.c
  - https://arm-software.github.io/CMSIS-DSP/latest/group__BiquadCascadeDF1.html
  - https://arm-software.github.io/acle/mve_intrinsics/mve.html
  - https://docs.scipy.org/doc/scipy/reference/generated/scipy.signal.sosfilt.html
aliases:
  - Helium DF1 biquad vectorization
  - MVE recursive biquad coefficient layout
  - DF1 recurrence SIMD expansion
  - techniques-helium-df1-biquad-vectorization
  - fourth-order IIR SIMD expansion
  - MVE fourth-order DF1 IIR
  - four output recurrence substitution
  - 4x12 IIR coefficient matrix
  - MVE Q31 DF1 feedback-state semantics
  - structural zero lanes in recursive SIMD
  - two cascaded biquads to fourth-order coefficients
  - IIR section coefficient convolution
evidence_basis: "CMSIS-DSP v1.18.0 public sources support the second-order DF1 F32/F16/Q31 implementation details and coefficient layouts. The fourth-order 4-by-12 transform is an independent algebraic derivation from the scalar recurrence, not a CMSIS-DSP fourth-order API or a performance result. ACLE documents the four-lane MVE operations used as a conceptual implementation basis."
---

Use this page when feedback dependencies limit lane-independent SIMD work in a direct-form-I (DF1) biquad or a fixed-coefficient higher-order IIR. Substituting earlier outputs within a group can expose several outputs as vector lanes. The fourth-order expansion below is an independently derived recurrence transform; CMSIS-DSP documents cascaded second-order sections, not a direct fourth-order API.

## Public CMSIS-DSP DF1 biquad baseline

For the positive-feedback convention used here, a second-order DF1 stage computes `y[i] = a1*y[i-1] + a2*y[i-2] + b0*x[i] + b1*x[i-1] + b2*x[i-2]`. If a filter design is written with subtracted feedback terms, negate those feedback coefficients before applying the positive-feedback formulas, and match the coefficient convention expected by the selected library API. Substituting earlier outputs into later equations exposes multiply-accumulate work while preserving the recurrence. The public CMSIS-DSP v1.18.0 [F32 initializer](https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/FilteringFunctions/arm_biquad_cascade_df1_init_f32.c) and [F16 initializer](https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/FilteringFunctions/arm_biquad_cascade_df1_init_f16.c) expand recurrence coefficients for MVE processing; their matching [F32 kernel](https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/FilteringFunctions/arm_biquad_cascade_df1_f32.c) and [F16 kernel](https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/FilteringFunctions/arm_biquad_cascade_df1_f16.c) consume those layouts.

For four F32 outputs, the expanded terms span six input positions, `x[i-2]` through `x[i+3]`, plus the two initial output-history values `y[i-2]` and `y[i-1]`. The F32 initializer builds a 4 by 8 coefficient array, advances it through four recurrence rows, and transposes it into the modified-coefficient structure. For eight F16 outputs, the span is ten input positions, `x[i-2]` through `x[i+7]`, plus the same two initial output states; the F16 initializer builds an 8 by 12 array and advances it through eight recurrence rows before transposing it. Each second-order stage needs two prior input and two prior output values.

The processing kernels handle four F32 outputs or eight F16 outputs per vector group and carry the two prior input and two prior output values into the next group. They load expanded coefficient vectors during the group and interleave those loads with vector multiply/FMA operations. Keeping fewer coefficient vectors live can reduce register pressure at the cost of more coefficient-load traffic. Treat these modified layouts as release-specific; use the public MVE initializer paired with the selected release's kernel. Static coefficient-generation helpers are implementation details, not public callable APIs.

The public [CMSIS-DSP v1.18.0 Q31 DF1 source](https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/FilteringFunctions/arm_biquad_cascade_df1_q31.c) uses an `int64_t` accumulator with vector dot-product and feedback accumulation, applies the configured shift, then extracts the high 32-bit result. It computes dependent outputs in sequence: after the first result, it places that value in the feedback state before calculating the next output, then carries both outputs into the following pair. Its staged coefficient vectors are `{b2,b1,b0,0}` and `{0,b2,b1,b0}` for input terms, and `{0,0,a2,a1}` and `{0,0,a1,a2}` for feedback terms. The MVE loop is unrolled to process four outputs, with results written in pairs. Preserve the selected release's Q31 scaling, accumulator, and feedback semantics; do not infer rounding or saturation behavior from another datatype.

## Independently derive a four-output fourth-order transform

For a fixed-coefficient fourth-order recurrence, write the scalar equation as `y[n] = sum(k=0..4, b[k]*x[n-k]) + sum(k=1..4, a[k]*y[n-k])`, using the positive-feedback sign convention. If the design equation subtracts feedback, negate those coefficients before building the rows. This section derives a four-output transform from that equation. It is general algebra, not a public CMSIS-DSP fourth-order function or a claim about its speed.

For a group starting at `i`, order the source scalars from older to newer as:

`z = (x[i-4], x[i-3], x[i-2], x[i-1], x[i], x[i+1], x[i+2], x[i+3], y[i-4], y[i-3], y[i-2], y[i-1])`.

Let `D_r` be a row of 12 coefficients for output `y[i+r]` before substituting outputs from the current group, for `r` from 0 through 3. In `D_r`, assign `b[k]` to the column for `x[i+r-k]` for each input tap `k` from 0 through 4. Assign `a[k]` to the column for `y[i+r-k]` only when `k > r`, because only those feedback terms refer to output history before this group. Set all other columns to zero. Then define the substituted rows:

```text
C_0 = D_0
C_1 = D_1 + a1*C_0
C_2 = D_2 + a1*C_1 + a2*C_0
C_3 = D_3 + a1*C_2 + a2*C_1 + a3*C_0
```

Each `C_r` has 12 coefficients. Stack them into a 4 by 12 matrix `C`. Transpose its columns into 12 four-lane vectors, `V_j = (C_0j, C_1j, C_2j, C_3j)`. In exact arithmetic, the four outputs are:

```text
(y[i], y[i+1], y[i+2], y[i+3]) = sum(j=0..11, z[j] * V_j)
```

This organization exposes four outputs as the lanes of an MVE F32 vector: each source scalar multiplies one coefficient vector, and the partial vectors can be accumulated. The [ACLE MVE intrinsics reference](https://arm-software.github.io/acle/mve_intrinsics/mve.html) documents four-lane F32 vectors, scalar-vector multiplication, and fused multiply-accumulate operations. The recurrence derivation determines the coefficient vectors; it does not prescribe a particular instruction schedule.

Precompute the 12 coefficient vectors while `a` and `b` are fixed, and rebuild them when the coefficients change. A transform built from one coefficient set is not valid for a group whose coefficients change between its four outputs; do not reuse fixed vectors across within-group coefficient modulation. For coefficient changes that must take effect inside a group, use a recurrence implementation or derive and validate a transform for that change pattern.

## State, bounds, and numerical behavior

At a group starting at `i`, the full transform uses input positions `x[i-4]` through `x[i+3]` and output-history values `y[i-4]` through `y[i-1]`. After processing all four outputs, retain `x[i]` through `x[i+3]` and `y[i]` through `y[i+3]` in the implementation's declared state order for the next group. For exact in-place execution, stage the group's new input samples and preserve its initial history before writing outputs, so an early store cannot replace an input needed by a later output. Apply the overlap behavior documented by the API being implemented.

For a short final group of `r` outputs, the first `r` rows of the full transform are mathematically valid; later-output input columns in those rows have zero coefficients. The implementation must still avoid reading nonexistent inputs and commit history only for valid outputs. Check the row selection and loads for the actual remaining count, or use a scalar tail.

The fourth-order row substitution is algebraically equivalent over exact arithmetic. Floating-point reassociation and fused multiply-accumulate can change rounding relative to the scalar evaluation order. Compare results against the chosen reference using nonzero initial state, group boundaries, long runs, the application's coefficient and input ranges, and its error tolerance. A direct fourth-order recurrence and a cascade of second-order sections can have different finite-precision behavior, so evaluate the actual filter rather than assuming equal results. CMSIS-DSP represents higher-order filters as cascaded biquads. As a separate numerical example, [SciPy's `sosfilt`](https://docs.scipy.org/doc/scipy/reference/generated/scipy.signal.sosfilt.html) uses cascaded second-order sections in direct-form-II-transposed form and describes that choice as reducing numerical precision errors for high-order filters; that rationale does not establish which realization is more accurate for a given application. For the public CMSIS-DSP representation, see [Biquad Cascade DF1](https://arm-software.github.io/CMSIS-DSP/latest/group__BiquadCascadeDF1.html).

For current CMSIS-DSP initialization and modified-coefficient storage, see [CMSIS-DSP Helium migration](../patterns/patterns-cmsis-dsp-helium-migration.md).

## Choose a lane mapping that matches the recurrence dependencies

The four-output expansion above substitutes within-group output dependencies, allowing that group to be accumulated across output lanes. Feedback from the group remains in the history used by the next group, and structural zero coefficients still occupy vector positions without contributing products. When additional independent work is available, map separate channels or suitably delayed and pipelined cascade-stage work to lanes; consecutive cascade stages processing the same sample remain dependent. Choose among these mappings based on available work and the state and layout costs. The public CMSIS-DSP DF1 kernels provide the release-specific implementation baseline; a custom lane mapping is a separate implementation choice. See [DF2T channel and delayed-stage lane mappings](techniques-helium-df2t-lane-mapping.md) for those alternatives.

The Q31 DF1 kernel uses staged vector dot products but reinjects each computed, scaled output into the feedback state before calculating the next output. Its 64-bit accumulation, configured shift, high-half extraction, and sequential state update are part of the fixed-point behavior. Do not transfer the floating-point coefficient-substitution transformation mechanically to Q31 or reorder its operations as if it were an exact floating-point recurrence. Preserve the selected kernel's scaling and feedback contract, and validate any changed operation order against the public implementation and application error bounds. An instruction's rounding name does not by itself specify the full routine's saturation or rounding behavior.

## Optionally convert two cascaded biquads to a fourth-order recurrence

When an implementation deliberately combines two fixed-coefficient biquad stages, derive the fourth-order coefficients from the transfer-function product and keep the sign convention explicit. For stage numerators `Bp(z)=bp0+bp1*z^-1+bp2*z^-2` and `Bq(z)=bq0+bq1*z^-1+bq2*z^-2`, the product numerator coefficients are `b[k]=sum(j=max(0,k-2)..min(2,k), bp[j]*bq[k-j])` for `k=0..4`. With positive-feedback recurrence coefficients, write the denominators as `Dp(z)=1-ap1*z^-1-ap2*z^-2` and `Dq(z)=1-aq1*z^-1-aq2*z^-2`. Their product, represented as `1-a1*z^-1-a2*z^-2-a3*z^-3-a4*z^-4`, gives:

```text
a1 = ap1 + aq1
a2 = ap2 + aq2 - ap1*aq1
a3 = -(ap1*aq2 + ap2*aq1)
a4 = -ap2*aq2
```

This is a coefficient conversion for an optional realization change. It is exact for zero initial state in exact arithmetic; it does not convert the two stages' nonzero internal histories into direct-form fourth-order state. A correct state mapping is required when preserving nonzero history, so copying the two biquads' state values into the direct recurrence is invalid. CMSIS-DSP v1.18.0 documents cascaded biquad processing rather than a public direct fourth-order function; retain the selected public kernel as the baseline. Recheck coefficient quantization, finite-precision behavior, overflow limits, and stability of the realized filter before adopting a transformed implementation. See [CMSIS-DSP Biquad Cascade DF1](https://arm-software.github.io/CMSIS-DSP/latest/group__BiquadCascadeDF1.html) and [SciPy `sosfilt`](https://docs.scipy.org/doc/scipy/reference/generated/scipy.signal.sosfilt.html) for public second-order-section context.

The four-output fourth-order expansion above forms twelve coefficient vectors from eight input positions (four prior inputs and four current inputs) and four output-history values. The MVE vector-register budget cannot hold all twelve vectors at once. Keep coefficient vectors streamed through the loop and schedule their loads among independent multiply/FMA work only where the target permits; this trades coefficient traffic against live-register pressure. When the coefficients are fixed, build the transformed coefficient layout outside the sample loop and rebuild it when the coefficients change. Do not infer a speed advantage from the algebraic conversion or from reducing structural zero terms.
