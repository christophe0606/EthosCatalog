---
id: techniques-helium-fft-butterflies
title: Map CFFT and RFFT butterflies to Helium complex operations
type: techniques
architectures:
  - armv8.1-m
  - cortex-m55
tags:
  - helium
  - mve
  - floating-point
  - fft-butterfly-vectorization
  - instruction-scheduling
  - c
  - asm
confidence: source-reported
reproducibility: concept
related:
  - techniques-helium-instruction-interleaving
  - hardware-dtcm-scatter-gather-banks
  - languages-c-float16-half-precision
  - techniques-profile-guided-helium-optimization
  - techniques-helium-fixed-point-arithmetic
  - techniques-helium-vector-reductions
sources:
  - https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/TransformFunctions/arm_cfft_f32.c
  - https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/TransformFunctions/arm_cfft_f16.c
  - https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/TransformFunctions/arm_rfft_fast_f32.c
  - https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/TransformFunctions/arm_bitreversal.c
  - https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/PrivateInclude/arm_vec_fft.h
  - https://github.com/Arm-Examples/Helium-Optimization/blob/bc9dfb67be64a2a5ee8e5325910a3f060a593863/Performance_analysis/ml-dsp-front-end-optimization-with-helium.md#L1372-L1480
  - https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf
  - https://developer.arm.com/community/arm-research/b/articles/posts/making-helium-going-around-in-circles
aliases:
  - Helium FFT butterflies
  - MVE complex butterfly vectorization
  - CFFT radix 2 radix 4 MVE
  - RFFT recombination Helium
  - M55 FFT software pipelining workflow
evidence_basis: "CMSIS-DSP v1.18.0 source links corroborate FFT butterfly layouts, complex operations, and permutation details."
---

Use these layouts when implementing MVE complex FFT kernels or the CFFT portion of a real FFT. In CMSIS-DSP v1.18.0, the [F32 CFFT](https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/TransformFunctions/arm_cfft_f32.c) and [F16 CFFT](https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/TransformFunctions/arm_cfft_f16.c) sources implement radix butterfly paths, while the private FFT helper header defines complex-operation macros and MVE bit-reversal helpers. These links describe release-specific implementation details.

## Radix-2 complex pairs

An interleaved complex vector stores real and imaginary components in adjacent lanes. A float32x4_t vector can therefore carry two complex values, and a float16x8_t vector can carry four. For a twiddled radix-2 branch, form the sum and difference vectors, load twiddle values in the layout required by the loop, then apply the complex product to the difference branch. Verify lane order and gather offsets against the selected data layout and intrinsic definitions.

The CMSIS-DSP v1.18.0 private [arm_vec_fft.h complex macros](https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/PrivateInclude/arm_vec_fft.h) define the three floating-point operations used here:

~~~c
#define MVE_CMPLX_ADD_A_ixB(A, B)           vcaddq_rot90(A,B)
#define MVE_CMPLX_SUB_A_ixB(A,B)            vcaddq_rot270(A,B)
#define MVE_CMPLX_MULT_FLT_Conj_AxB(A,B)    vcmlaq_rot270(vcmulq(A, B), A, B)
~~~

The header also contains a non-conjugated complex-multiply macro. These are private implementation macros guarded for the library's MVE build, not public callable APIs. The released CFFT files show their use in concrete kernels; consult the exact release and compiler intrinsic interface before copying a kernel schedule.

## Radix-4 main loop and final stage

For radix 4, form vector sums and differences for input pairs A/C and B/D. The first output combines the two sums without a twiddle; the other branches combine differences, apply the required plus or minus i rotation, and multiply by their twiddle vectors. The public F32 and F16 CFFT sources implement radix butterfly stages using vector inputs and twiddle data. Preserve the branch and rotation order when adapting their layout.

In the final vector stage, the public [F32 CFFT](https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/TransformFunctions/arm_cfft_f32.c) and [F16 CFFT](https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/TransformFunctions/arm_cfft_f16.c) implementations gather irregular lane positions, form butterfly sums and differences without loading twiddle vectors, then scatter the results. Treat this organization as specific to these released kernels; verify the selected transform, data layout, and direction before adapting it.

The public [MVE bit-reversal helpers](https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/PrivateInclude/arm_vec_fft.h) implement in-place permutation using a bit-reversal table with vector gather and scatter operations. Their helper names are internal implementation details; do not treat them as public APIs. The [CFFT entry points](https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/TransformFunctions/arm_cfft_f32.c) select bit reversal according to the function's bitReverseFlag, so follow that release's documented flag and transform contract.

An out-of-place bit-reversal design can gather values in permutation order into a separate destination buffer, while an in-place design uses a permutation table and swaps/scatters within the working buffer. The out-of-place approach needs additional destination storage; the in-place approach avoids that second buffer but has different access and permutation costs. CMSIS-DSP v1.18.0 provides the in-place MVE helper as a public-source example; this comparison describes a general storage trade-off, not a second CMSIS API or a universal speed ranking.

## RFFT recombination

The public [CMSIS-DSP v1.18.0 fast F32 RFFT](https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/TransformFunctions/arm_rfft_fast_f32.c) performs a CFFT and then extracts the real spectrum by traversing one complex half forward and the other backward, conjugating as needed, and combining the pair with real-FFT twiddle factors. This is the implementation basis for the lane-layout pattern; endpoint packing, scaling, and direction handling are part of the function's full implementation and must be preserved when adapting it.

## Consider software pipelining only for a profiled hot loop

For a profiled Cortex-M55 FFT inner loop, inspect adjacent operations that compete for the same execution resources or depend on each other's results. Software pipelining may overlap independent work from neighboring iterations, but requires a valid prologue and epilogue, correct loop bounds and tail handling, and enough registers to hold staged values without spills.

Use static scheduling analysis to form a hypothesis, then inspect the generated instructions and verify the complete kernel on the intended processor. Do not treat the [ML/DSP front-end optimization case study](https://github.com/Arm-Examples/Helium-Optimization/blob/bc9dfb67be64a2a5ee8e5325910a3f060a593863/Performance_analysis/ml-dsp-front-end-optimization-with-helium.md)'s assembly alternatives or simulator analysis as a guaranteed improvement. When substituting a CMSIS-DSP FFT function, preserve the selected API's data representation, ordering, scaling, direction, and boundary behavior.

## Preserve complex lane and arithmetic semantics

Keep each complex value's real and imaginary components adjacent, in the order required by the selected ACLE operation. A 128-bit vector holds two F32 complex pairs or four F16 complex pairs. Check the operation's current intrinsic definition and public CMSIS-DSP implementation for its lane convention; do not derive lane order from a conflicting diagram or table. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 89–91](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

A floating-point complex product combines complementary rotation contributions: use the VCMUL product and the appropriate VCMLA rotation for the other cross-products, choosing conjugation and sign from the butterfly branch and transform direction. Preserve the fused multiply-accumulate contract when adapting the sequence. VCMLA fuses multiplication and addition but rounds its result to the destination precision; its result can differ from separate multiply and add operations. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 90–92](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

Fixed-point complex multiplication uses the saturating doubling multiply add/subtract family with high-half results, including the exchange forms that place the paired result in the other element of a complex pair. Match the add/subtract and exchange pattern to the real/imaginary products, and retain the selected lane order, scaling, saturation, and optional rounding contract. These are fixed-point integer instructions; floating-point VCMUL and VCMLA are not substitutes for them. See [Helium fixed-point arithmetic](techniques-helium-fixed-point-arithmetic.md). [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 89, 92](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

## Generate permutation addresses from logical sample indices

For a radix and permutation that use binary bit reversal, generate an incrementing sequence of logical sample indices and apply VBRSR to the number of index bits dictated by the transform size. This reverses address indices, not sample values. Convert each resulting sample index to the address of a complete interleaved complex record, applying the byte or element scaling required by the selected gather/scatter form. Mixed-radix permutations do not generally follow a simple binary-reversal formula; use the matching permutation table or documented library path. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 188–189](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

[VBRSR and gather/scatter design rationale](https://developer.arm.com/community/arm-research/b/articles/posts/making-helium-going-around-in-circles).

When permuting in place, keep every sample intact until it has been read for its destination; use a proven swap/permutation schedule rather than overwriting a still-needed value. An out-of-place gather has a distinct contract and requires separate destination storage. Keep the exact transform size, radix stages, direction, twiddle layout, output ordering, normalization, and real-transform endpoint packing consistent with the selected public API. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 188–190](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)
