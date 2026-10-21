---
id: patterns-cmsis-dsp-helium-migration
title: Migrate CMSIS-DSP applications to Helium library paths and aligned buffers
type: patterns
architectures:
  - cortex-m55
  - cortex-m85
tags:
  - cmsis-dsp-migration
  - buffer-alignment
  - helium
  - mve
  - c
  - cpp
  - half-precision
  - floating-point
confidence: source-reported
reproducibility: concept
related:
  - techniques-arm-compiler-mve-target-configuration
  - techniques-helium-auto-vectorization
  - hardware-itcm-instruction-data-contention
  - hardware-dtcm-scatter-gather-banks
  - languages-c-float16-half-precision
  - techniques-helium-fir-tap-blocking
  - techniques-helium-df1-iir-vectorization
  - patterns-helium-small-kernel-fusion
  - techniques-helium-fft-butterflies
  - techniques-helium-matrix-kernel-layout
  - techniques-helium-vector-reductions
  - techniques-helium-predication-and-tail-handling
  - techniques-helium-memory-access-layout
sources:
  - https://support.arm.com/documentation/102095/0101/?lang=en
  - https://developer.arm.com/community/arm-community-blogs/b/architectures-and-processors-blog/posts/armv8_2d00_m-based-processor-software-development-hints-and-tips
  - https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/FilteringFunctions/arm_fir_f32.c
  - https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/FilteringFunctions/arm_fir_q15.c
  - https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/FilteringFunctions/arm_fir_q31.c
  - https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/FilteringFunctions/arm_fir_init_f32.c
  - https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/FilteringFunctions/arm_biquad_cascade_df1_init_f32.c
  - https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/FilteringFunctions/arm_biquad_cascade_df1_init_f16.c
  - https://github.com/Arm-Examples/Helium-Optimization/blob/bc9dfb67be64a2a5ee8e5325910a3f060a593863/General_optimizations/basic_optimization_topics.md#L80-L107
  - https://github.com/Arm-Examples/Helium-Optimization/blob/bc9dfb67be64a2a5ee8e5325910a3f060a593863/Performance_analysis/ml-dsp-front-end-optimization-with-helium.md#L573-L757
  - https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/ComplexMathFunctions/arm_cmplx_mag_squared_f32.c
  - https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/StatisticsFunctions/arm_power_f32.c
  - https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/BasicMathFunctions/arm_dot_prod_f32.c
  - https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/BasicMathFunctions/arm_mult_f32.c
  - https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/ComplexMathFunctions/arm_cmplx_mult_real_f32.c
  - https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/SupportFunctions/arm_q15_to_float.c
  - https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf
  - https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/MatrixFunctions/arm_mat_vec_mult_f16.c
  - https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/BasicMathFunctions/arm_add_f16.c
  - https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/StatisticsFunctions/arm_max_no_idx_f16.c
  - https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/FastMathFunctions/arm_vexp_f16.c
  - https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Include/arm_vec_math_f16.h
  - https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Include/arm_helium_utils.h
aliases:
  - CMSIS-DSP arm_math.h build integration
  - CMSIS-DSP MVE porting
  - CMSIS-DSP Cortex-M55 migration
  - CMSIS-DSP aligned buffers
  - Helium biquad initialization
  - CMSIS-DSP block-size selection
  - replace scalar DSP loops with CMSIS-DSP APIs
  - CMSIS-DSP F16 matrix-vector and bias composition
  - CMSIS-DSP F16 extrema and vector exponential
  - MVE F16 public math APIs
  - CMSIS-DSP two mono FIR stereo wrapper
  - interleaved stereo FIR with independent channel state
prerequisites:
  - Build the CMSIS-DSP source for the processor's actual feature configuration.
  - Check the documentation for the specific CMSIS-DSP release and function before changing buffer sizes or padding.
---

When moving a CMSIS-DSP application to Cortex-M55 or Cortex-M85 with Helium, most application calls may remain usable, but some MVE-specific initialization, coefficient storage, and buffer alignment details need review. The [Armv8.1-M software development hints and tips article](https://developer.arm.com/community/arm-community-blogs/b/architectures-and-processors-blog/posts/armv8_2d00_m-based-processor-software-development-hints-and-tips) describes CMSIS-DSP releases as source-only because processor configurability makes shipping every binary variant impractical.

For the Arm Compiler 6 library build, the [Armv8.1-M software development hints and tips article](https://developer.arm.com/community/arm-community-blogs/b/architectures-and-processors-blog/posts/armv8_2d00_m-based-processor-software-development-hints-and-tips) recommends `-Ofast` for performance. This is an article-specific recommendation, and `-Ofast` can change floating-point semantics; confirm that those changes meet the application's numerical requirements.

For the MVE biquad DF1 path, replace the non-Helium initializer with the MVE-specific initializer and provide its modified-coefficient buffer:

| Non-Helium initializer | MVE initializer | Additional storage |
| --- | --- | --- |
| `arm_biquad_cascade_df1_init_f32` | `arm_biquad_cascade_df1_mve_init_f32` | `pCoeffsMod`, sized to `32*numStages` `float32_t` elements |

Some Helium FIR functions may require padding the coefficient array. The [Armv8.1-M software development hints and tips article](https://developer.arm.com/community/arm-community-blogs/b/architectures-and-processors-blog/posts/armv8_2d00_m-based-processor-software-development-hints-and-tips) does not give a universal padding formula: use the requirements for the exact function and CMSIS-DSP release in its matching documentation. Review older applications for calls on the deprecated-function list and update affected calls as appropriate.

For filter-processing buffers, the [Armv8.1-M software development hints and tips article](https://developer.arm.com/community/arm-community-blogs/b/architectures-and-processors-blog/posts/armv8_2d00_m-based-processor-software-development-hints-and-tips) recommends at least 64-bit (8-byte) alignment and says 128-bit (16-byte) alignment is better. Treat this as alignment guidance, and preserve any separate storage and size requirements of the selected function.

## Coefficient layout in the public MVE implementations

The release-pinned [CMSIS-DSP F32 FIR implementation](https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/FilteringFunctions/arm_fir_f32.c), [Q15 FIR implementation](https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/FilteringFunctions/arm_fir_q15.c), and [Q31 FIR implementation](https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/FilteringFunctions/arm_fir_q31.c) show their own state and coefficient handling. Do not infer a current function's allocation contract from a generic FIR schematic: use the selected release's function documentation for state length, padding, coefficient order, and scratch requirements.

For MVE DF1, the public [F32 initializer](https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/FilteringFunctions/arm_biquad_cascade_df1_init_f32.c) uses a static internal helper to expand a 4 by 8 recurrence coefficient array and transpose it into the modified-coefficient structure. The public [F16 initializer](https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/FilteringFunctions/arm_biquad_cascade_df1_init_f16.c) similarly expands an 8 by 12 array and transposes it. The helpers named `generateCoefsFastBiquadF32` and `generateCoefsFastBiquadF16` are static functions inside those implementation files, not public callable APIs. The F32 and F16 MVE initializer functions are public entry points in the release sources; use the matching declarations and structure types for that release. The modified layout is an implementation detail, so do not infer a universal F16 allocation size from the temporary array dimensions.

See [Helium DF1 IIR vectorization](../techniques/techniques-helium-df1-iir-vectorization.md) for the public biquad coefficient-expansion references, derived higher-order layouts, and numerical validation guidance. For FIR tap scheduling and the scratch-pass tradeoff, see [Helium FIR tap blocking](../techniques/techniques-helium-fir-tap-blocking.md).

## Select block size and library paths

CMSIS-DSP routines are intended to process blocks. For very short blocks, call and loop setup can be a meaningful part of the implementation, so compare a library call with a small dedicated kernel for the application's actual block size. This is a measured design choice, not a universal threshold; see [small-kernel fusion](patterns-helium-small-kernel-fusion.md) for ways to reduce repeated setup and intermediate traffic. Build the library for the processor's implemented MVE and floating-point features, and check the selected release's function and buffer contracts.

Start by selecting a semantically matching optimized CMSIS-DSP routine for DSP work, or an optimized CMSIS-NN kernel through the application's runtime for supported neural-network operators; see [CMSIS-NN MVE runtime selection](patterns-cmsis-nn-mve-runtime-selection.md). For remaining custom C or C++ loops, inspect compiler vectorization diagnostics and generated code. Use Helium intrinsics or assembly only for a residual hotspot that profiling shows still needs attention, then validate its numerical and memory contracts. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 129–133](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf) A library reduces implementation effort within the interfaces it provides. [Coding for Helium v1.1, §3, pp. 8–9](https://support.arm.com/documentation/102095/0101/?lang=en)

## Integrate the selected CMSIS-DSP release

Include `arm_math.h` for CMSIS-DSP function declarations. The Coding for Helium v1.1 guide's variance example composes `arm_fill_f32`, `arm_dot_prod_f32`, and `arm_mult_f32`; use it to understand library composition, not as an optimal multi-call recipe for every block size. [Coding for Helium v1.1, §5, pp. 13–14](https://support.arm.com/documentation/102095/0101/?lang=en)

Make the selected CMSIS-DSP release's headers and built sources or libraries available to the application, and compile the relevant objects for compatible implemented MVE and floating-point features. An application target flag alone does not establish that a separately built library contains the desired MVE path; verify the selected release's build inputs and implementation. [Coding for Helium v1.1, §5, pp. 14–15](https://support.arm.com/documentation/102095/0101/?lang=en)

Study public CMSIS-DSP implementations and examples to understand lane, buffer, and numerical contracts before maintaining a replacement. Use the [ARM-software/CMSIS-DSP repository](https://github.com/ARM-software/CMSIS-DSP) and verify contracts against the selected release; the CMSIS-DSP implementation links elsewhere on this page are pinned to v1.18.0. [Coding for Helium v1.1, §10, p. 34](https://support.arm.com/documentation/102095/0101/?lang=en)

## Replace custom operations with equivalent CMSIS-DSP functions

Before maintaining an arithmetic loop, check CMSIS-DSP for a function with matching semantics. These case-study candidates have public implementations in release v1.18.0:

| Operation | Candidate API and release source |
| --- | --- |
| Complex magnitude squared | [arm_cmplx_mag_squared_f32](https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/ComplexMathFunctions/arm_cmplx_mag_squared_f32.c) |
| Sum of squares | [arm_power_f32](https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/StatisticsFunctions/arm_power_f32.c) |
| Real dot product | [arm_dot_prod_f32](https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/BasicMathFunctions/arm_dot_prod_f32.c) |
| Elementwise multiplication / windowing | [arm_mult_f32](https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/BasicMathFunctions/arm_mult_f32.c) |
| Complex samples multiplied by real weights | [arm_cmplx_mult_real_f32](https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/ComplexMathFunctions/arm_cmplx_mult_real_f32.c) |
| Q15 to F32 conversion | [arm_q15_to_float](https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/SupportFunctions/arm_q15_to_float.c) |

Basic add, subtract, and fill loops also have library equivalents. Treat each function as a candidate replacement and check its declarations, implementation, and allocation requirements in the selected release.

CMSIS-DSP also provides classical machine-learning functions in areas including SVM classification, Bayesian estimators, distance calculations, weighted-sum support functions, and statistics such as entropy and Kullback–Leibler divergence. These are library-discovery candidates when they match the application's task; the Arm Helium reference book describes its functions as single-precision and reports Helium implementations for many of them. Availability and MVE support vary by CMSIS-DSP release, so verify each selected release's supported data type, input layout, approximation behavior, and allocation contract before integrating a function. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 230–231](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

Select a public CMSIS-DSP classical-ML function by matching its task semantics, input type, and configured MVE support in the release being built. Treat the Arm Helium reference book's named vector paths and approximation helpers, including reciprocal or logarithm helpers where used, as a 2020 snapshot: verify current declarations, contracts, and approximation behavior in that release, and do not infer availability from Helium target flags alone. Do not change the model algorithm solely to fit an instruction; first establish that a candidate function preserves the task semantics. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 230–231](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

Match each function's element count, input/output layout, aliasing rules, numeric format, and accumulation behavior. In particular, complex routines may count complex samples while buffers hold separate real and imaginary values. Preserve fixed-point scaling, saturation and rounding, or floating-point evaluation behavior where those are part of the caller's contract. Check boundary lengths and tails rather than copying an example's count mechanically.

## Compose public F16 matrix and math operations

A row-major F16 dense layer can map to `arm_matrix_instance_f16` with `numRows` set to the output count, `numCols` to the input count, and `pData` pointing at the weights. The public `arm_mat_vec_mult_f16` operation produces row sums; `arm_add_f16` can then add the bias over the output count. This is a reference decomposition for matrix-vector multiplication followed by vector addition. Check the selected [CMSIS-DSP v1.18.0 matrix-vector](https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/MatrixFunctions/arm_mat_vec_mult_f16.c) and [vector-add](https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/BasicMathFunctions/arm_add_f16.c) contracts, enabled F16 build path, dimensions, zero-size behavior, arithmetic range, and buffer overlap before using it in place of a scalar loop.

Audit memory access as well as API names. In the selected matrix-vector implementation, the final input load is predicated but corresponding weight loads and FMA are full width. The vector-add tail also reads complete source vectors while only its output store is predicated. Use these routines directly only when every full-width access is within valid storage and padded values satisfy the arithmetic contract, including the intermediate output consumed by the add; otherwise provide a bounded tail or scalar cleanup. Exact output-in-place support in the selected add implementation does not establish arbitrary partial-overlap safety. Preserve unread inputs, weights, and bias.

For elementwise extrema and exponential work, consider public `arm_max_no_idx_f16` and `arm_vexp_f16` when their semantics fit. The F16 exponential uses a vector approximation for full groups and scalar `expf` for leftover elements, so validate accuracy and range across both paths. Its vector helper is a range-reduced polynomial implementation, not a hardware exponential instruction; very small values can underflow during reconstruction. The maximum implementation seeds with a finite minimum and uses number-max operations, so check NaN, infinity, and empty-input requirements rather than assuming every custom comparison loop has equivalent behavior. See [MVE reductions](../techniques/techniques-helium-vector-reductions.md) and [invariant output work](patterns-helium-small-kernel-fusion.md). `vecAddAcrossF16Mve` in the public support header is a library helper, not an architectural horizontal-add instruction or a stable standalone DSP API; pin and inspect it if a maintained custom kernel depends on it.

## Preserve the FFT wrapper contract

A wrapper around a native CMSIS-DSP FFT can ease a staged migration, while moving all consumers to the native representation may remove conversions later. Before replacing the wrapper, document and test the caller-visible ordering, forward or inverse direction, normalization/scaling, and the representation of packed real-spectrum endpoints. Check round-trip behavior and downstream consumers against the selected release. The [ML/DSP front-end optimization case study](https://github.com/Arm-Examples/Helium-Optimization/blob/bc9dfb67be64a2a5ee8e5325910a3f060a593863/Performance_analysis/ml-dsp-front-end-optimization-with-helium.md) contains sample code whose endpoint and length details should not be generalized into a reusable contract; derive those details from the public API used by the application.

## Match FFT output counts and magnitude semantics

When passing a complex FFT result to a magnitude routine, use the complex-sample count and interleaved layout required by that function, not the scalar component count. For a real FFT, first confirm whether its public API returns a packed spectrum and how the endpoints are represented, then derive the valid magnitude range from that contract. Helium has no vector square-root instruction; use a library path with matching semantics or validate any approximate vector square root for the required error, exceptional values, and input domain before adopting it. The Arm Helium reference book's magnitude discussion is an implementation example, not a stable private-helper API. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 190–191](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

## Initialize FIR coefficients and state for the selected release

The Arm Helium reference book's FIR example uses time-reversed coefficients, with the oldest sample paired to the first coefficient; confirm coefficient order against the selected CMSIS-DSP function and data convention before reusing a coefficient array. Coefficients may be shared between filter instances when their contents and layout are compatible, but each independent filter needs its own evolving state and instance structure. Initialize each instance's state, coefficient and state pointers, and tap metadata through the selected release's public initialization routine or documented contract. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 195–196](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

Check that exact CMSIS-DSP release and FIR function for state size, zero-initialization requirements, coefficient padding, and any scratch storage. These requirements can differ between scalar and MVE paths; do not generalize an older example's state formula, padding, or dispatch conditions into a universal allocation rule. The release-pinned public [F32 FIR](https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/FilteringFunctions/arm_fir_f32.c), [Q15 FIR](https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/FilteringFunctions/arm_fir_q15.c), and [Q31 FIR](https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/FilteringFunctions/arm_fir_q31.c) implementations are release-specific references, not a substitute for that function's documented allocation contract. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF p. 196](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

## Compose two mono FIR instances for interleaved stereo frames

If the available public kernel is mono but the caller uses interleaved stereo, deinterleave `N` frames into one `N`-sample block per channel, call `arm_fir_f32` once per channel with a block size of `N`, then interleave the outputs in the original channel order. The block count is frames per channel, not `2*N` interleaved scalar components. Use the public [`arm_fir_init_f32`](https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/FilteringFunctions/arm_fir_init_f32.c) and [`arm_fir_f32`](https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/FilteringFunctions/arm_fir_f32.c) APIs from the selected release and build the library for the target's configured MVE features; do not depend on a historical `_mve`-suffixed function name.

Initialize two filter instances with two independent state buffers so each channel retains its own history across blocks. Coefficients may share storage only when both filters intentionally use identical taps in a compatible layout. The conversion scratch buffers are additional wrapper storage, not FIR history or state. Check the selected release's tap order, coefficient padding, state length, block-size and in-place contracts; allocate bounded reusable scratch for the supported block size instead of variable-length stack arrays derived from an unbounded input count. Handle conversion tails without reading or writing outside either interleaved or channel storage. For direct interleaved processing and the complete-group transfer/tail rules, see [Helium memory access and layout](../techniques/techniques-helium-memory-access-layout.md); for FIR tap/sample tails and history lifetime, see [Helium FIR tap blocking](../techniques/techniques-helium-fir-tap-blocking.md).
