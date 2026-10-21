---
id: patterns-helium-small-kernel-fusion
title: Fuse small producer and consumer DSP kernels
type: patterns
architectures:
  - armv8.1-m
  - cortex-m52
  - cortex-m55
  - cortex-m85
tags:
  - helium
  - mve
  - auto-vectorization
  - instruction-scheduling
  - half-precision
  - floating-point
confidence: source-reported
reproducibility: concept
related:
  - patterns-cmsis-dsp-helium-migration
  - techniques-helium-auto-vectorization
  - techniques-helium-instruction-interleaving
  - techniques-helium-matrix-kernel-layout
  - techniques-helium-vector-reductions
  - techniques-helium-fixed-point-arithmetic
  - languages-c-float16-half-precision
  - techniques-helium-predication-and-tail-handling
  - techniques-helium-memory-access-layout
  - techniques-helium-fft-butterflies
sources:
  - https://github.com/Arm-Examples/Helium-Optimization/blob/bc9dfb67be64a2a5ee8e5325910a3f060a593863/Performance_analysis/ml-dsp-front-end-optimization-with-helium.md#L849-L867
  - https://github.com/Arm-Examples/Helium-Optimization/blob/bc9dfb67be64a2a5ee8e5325910a3f060a593863/Performance_analysis/ml-dsp-front-end-optimization-with-helium.md#L1337-L1371
  - https://github.com/Arm-Examples/Helium-Optimization/blob/bc9dfb67be64a2a5ee8e5325910a3f060a593863/General_optimizations/optimizing_c_functions.md#L81-L98
  - https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf
  - https://github.com/ARM-software/CMSIS-DSP/tree/main/Source/MatrixFunctions
  - https://github.com/ARM-software/CMSIS-DSP/tree/main/Source/BasicMathFunctions
  - https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/MatrixFunctions/arm_mat_vec_mult_f16.c
  - https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/BasicMathFunctions/arm_add_f16.c
  - https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/StatisticsFunctions/arm_max_no_idx_f16.c
  - https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/FastMathFunctions/arm_vexp_f16.c
  - https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Include/arm_vec_math_f16.h
  - https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/PrivateInclude/arm_vec_fft.h
  - https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/ComplexMathFunctions/arm_cmplx_mult_cmplx_f32.c
  - https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/BasicMathFunctions/arm_scale_f32.c
  - https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/StatisticsFunctions/arm_power_f32.c
  - https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/SupportFunctions/arm_float_to_q7.c
  - https://arm-software.github.io/acle/mve_intrinsics/mve.html
aliases:
  - Helium loop fusion
  - eliminate short DSP temporary buffers
  - inline short CMSIS-DSP call chains
  - fuse matrix-vector output scaling after row reduction
  - Helium F16 bias after row reduction
  - Invariant exponential output factor
  - F16 exponential reduction kernel fusion
  - Helium differential complex product
  - conjugate previous complex sample times current sample
  - fuse complex product scaling conversion and store
  - fused gain and post-gain energy
prerequisites:
  - Profile the complete application to confirm that the call chain is worth changing.
  - Define numerical, aliasing, and boundary-count behavior before combining operations.
---

Use this pattern for profiled short DSP pipelines where call, stack, loop setup, or intermediate-buffer traffic is significant relative to useful work. Keep the public CMSIS-DSP sequence as a reference and validate any fused replacement against the application's numerical and buffer contracts.

## Find a suitable producer and consumer

First inspect whether successive library calls store an intermediate that the next operation immediately reloads, or whether several operations reuse the same input values. Link-time optimization or inlining may expose opportunities without changing the algorithm. If that is not sufficient, a fused kernel can retain loaded values in registers and perform several operations before storing final outputs.

The [ML/DSP front-end optimization case study](https://github.com/Arm-Examples/Helium-Optimization/blob/bc9dfb67be64a2a5ee8e5325910a3f060a593863/Performance_analysis/ml-dsp-front-end-optimization-with-helium.md) gives complex conjugation followed by a complex dot product as an example of a short call chain with an immediately consumed temporary. Treat it only as a pattern: preserve real/imaginary ordering and conjugation sign, reduction precision and evaluation order, aliasing behavior, fixed-point scaling/saturation/rounding, and correct sample and tail counts. Do not copy the case-study loop mechanically.

## Check the cost of fusion

Fusion can remove intermediate stores, reloads, and repeated setup, but it can also raise register pressure, cause spills, increase code size, or change floating-point association. Compare the generated code and profile the complete application after the change. Keep the simpler library path when the fused implementation does not improve the actual workload or makes its contract harder to maintain. The [ML/DSP front-end optimization case study](https://github.com/Arm-Examples/Helium-Optimization/blob/bc9dfb67be64a2a5ee8e5325910a3f060a593863/Performance_analysis/ml-dsp-front-end-optimization-with-helium.md) describes a CMSIS-DSP C++ fusion facility as experimental at the time of writing; check the selected public release before depending on that facility.

Loop partitioning is the opposite option: separating several tasks may expose simpler vectorizable loops, but it can add traversals and intermediate traffic. Fusion can remove that traffic while keeping more values live and increasing spill risk. Compare both layouts using the actual working set, generated code, and complete workload; neither splitting nor fusion is a universal winner. See [Helium loop vectorization](../techniques/techniques-helium-auto-vectorization.md) for iteration-count, dependency, and compiler-diagnostic checks. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF p. 137](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

## Finalize a matrix output after its row sum

When a completed matrix-vector row immediately feeds independent output bias or scaling, keep that row's scalar accumulator live, apply the required final operation once after all columns have contributed, and store the final output. This can avoid an intermediate result store and reload. Do not apply rounding, scaling, or saturation to each partial dot product unless that is the specified arithmetic; it changes the result.

Preserve the complete output contract: raw signed 32-bit sum or quantized output, bias width and order, multiplier and shift, rounding mode, saturation, and output signedness. Add final bias or scaling only when the caller requires it. Bound the accumulator including any fused bias, and account for live per-row metadata, aliasing, tails, and register pressure when checking generated code. See [Helium matrix kernel layout](../techniques/techniques-helium-matrix-kernel-layout.md), [Helium vector reductions](../techniques/techniques-helium-vector-reductions.md), and [Helium fixed-point arithmetic](../techniques/techniques-helium-fixed-point-arithmetic.md).

Check the selected public [CMSIS-DSP matrix](https://github.com/ARM-software/CMSIS-DSP/tree/main/Source/MatrixFunctions) or [basic math](https://github.com/ARM-software/CMSIS-DSP/tree/main/Source/BasicMathFunctions) routine for its full contract before treating it as a reference. A quantized NN kernel may include bias and requantization that a raw matrix-vector result does not; such a routine is not interchangeable solely because its inner products look similar.

## Finalize F16 rows and hoist invariant exponential work

For an F16 dense row, keep the partial products in lane accumulators, reduce the completed row, add its bias once, and store the result. Seeding every lane with the full bias would add the bias once per lane during reduction. The fused and library paths may use different F16 rounding and contraction, so define the allowed numerical error before replacing one with the other. See [F16 row blocking](../techniques/techniques-helium-matrix-kernel-layout.md) and [pairwise reductions](../techniques/techniques-helium-vector-reductions.md).

**Mathematical derivation for real-valued inputs:** for a vector of length `N > 0`, let `m = max_i(x_i)` and `s = sum_i exp(x_i - m)`. For finite real inputs, `s` is positive. The expression `sqrt(s^2 / (N * exp(x_i)))` simplifies to `(s / sqrt(N)) * exp(-x_i / 2)`. The factor `s / sqrt(N)` is independent of output index, so compute it once after the max and exponential-sum reductions; then vectorize the per-element exponential and multiplication for valid outputs. This is an algebraic decomposition, not a standard softmax operation. The pass order remains max, sum, common-factor computation, then output because each later stage depends on the earlier reduction.

The real-number identity does not guarantee F16 equivalence. The original square or exponential can overflow or underflow; the transformed exponential and common factor have their own range limits. Narrowing, rounding, exceptional values, and the CMSIS-DSP vector exponential approximation can change results, and its scalar remainder may use `expf`. Set an input range and error tolerance and validate both paths before adopting the rewrite. Require `N > 0`, define empty-input behavior separately, and bound all input and output tails. See [CMSIS-DSP F16 math APIs](patterns-cmsis-dsp-helium-migration.md) for implementation details.

## Fuse a differential complex product with its consumer

When a complex product is immediately scaled, converted, and stored, keep the producer and its consumer in one pass if that removes an intermediate buffer without violating the numeric or aliasing contract. For `previous = p_r + j p_i` and `current = c_r + j c_i`, the derived product `conjugate(previous) * current` has real part `p_r*c_r + p_i*c_i` and imaginary part `p_r*c_i - p_i*c_r`. The operand order matters: using `previous` as operand A and `current` as B gives this sign; reversing them changes the imaginary result. The ACLE rotation form and the CMSIS-DSP private `Conj_AxB` helper illustrate this mapping; ordinary complex multiplication uses a different rotation. See [Helium FFT butterflies](../techniques/techniques-helium-fft-butterflies.md) for the already-covered lane layout and rotations. This formula is a mathematical derivation, not a public fused CMSIS-DSP API.

Hoist a factor shared by all components when the transformation is valid for the required precision and rounding. Preserve the caller's scale, conversion width, rounding, signed output range, and behavior for exceptional values. A byte-width store writes low output bits; it does not perform a numeric conversion, scaling, rounding, clipping, or saturation. Do not equate such a store with the distinct contract of [CMSIS-DSP `arm_float_to_q7`](https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/SupportFunctions/arm_float_to_q7.c), which includes saturating conversion. Define the output quantization contract explicitly and see [Helium memory access and layout](../techniques/techniques-helium-memory-access-layout.md) for narrowing semantics.

## Fuse in-place gain with post-gain energy

For interleaved complex samples `x_n = I_n + j Q_n` and a real gain `g`, keep the layout interleaved: load a vector such as `u = [I_0,Q_0,I_1,Q_1]`, compute `v = g*u` once in the chosen precision, store `v`, and accumulate its squares while it remains in a register. Across all valid lanes this produces `sum_n((g*I_n)^2 + (g*Q_n)^2)`, the total energy of the gained values. It is not the input energy and is not an average unless a separately defined normalization is applied. A public reference sequence is `arm_scale_f32` over the interleaved scalar components followed by `arm_power_f32` on those scaled components; the fused operation is a custom composition, not a CMSIS-DSP API. [CMSIS-DSP `arm_scale_f32`](https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/BasicMathFunctions/arm_scale_f32.c) [CMSIS-DSP `arm_power_f32`](https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/StatisticsFunctions/arm_power_f32.c)

Initialize lane partial sums to zero, include each real and imaginary component once, and reduce once after all valid elements. Load every current vector before its in-place store where required to preserve unread values; do not assume arbitrary partial overlap is safe. Define zero-input behavior, exclude inactive tail lanes, and account for changed FMA/reduction association and F16 range if those formats are used. See [Helium vector reductions](../techniques/techniques-helium-vector-reductions.md) for accumulation and tail rules.
