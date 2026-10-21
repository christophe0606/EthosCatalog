---
id: techniques-helium-df2t-lane-mapping
title: Map DF2T biquad work across Helium lanes
type: techniques
architectures:
  - armv8.1-m
tags:
  - helium
  - mve
  - floating-point
  - biquad-recurrence-vectorization
  - iir-recurrence-vectorization
  - c
  - asm
confidence: inferred
reproducibility: concept
related:
  - techniques-helium-df1-iir-vectorization
  - techniques-helium-instruction-interleaving
  - techniques-helium-memory-access-layout
  - techniques-helium-predication-and-tail-handling
  - techniques-helium-multiword-carry-arithmetic
sources:
  - https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/FilteringFunctions/arm_biquad_cascade_df2T_f32.c
  - https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/FilteringFunctions/arm_biquad_cascade_df2T_init_f32.c
  - https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/FilteringFunctions/arm_biquad_cascade_stereo_df2T_f32.c
  - https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/FilteringFunctions/arm_biquad_cascade_stereo_df2T_init_f32.c
  - https://arm-software.github.io/CMSIS-DSP/latest/group__BiquadCascadeDF2T.html
  - https://arm-software.github.io/acle/mve_intrinsics/mve.html
aliases:
  - Helium DF2T independent-channel vectorization
  - MVE four-channel biquad lanes
  - DF2T per-channel vector state
  - Helium DF2T delayed-stage pipeline
  - VSHLC biquad cascade lane shift
  - four-stage DF2T delayed-sample lane mapping
  - DF2T cascade startup and drain
  - VSHLC whole-vector sample pipeline
evidence_basis: "CMSIS-DSP v1.18.0 public sources provide F32 DF2T scalar, stereo, and initialization references. Independent-channel and delayed-stage vector schedules are independently derived implementation patterns; ACLE documents the MVE operations used."
---

Use these patterns when an F32 DF2T cascade has independent channels or when a single-channel cascade is being reorganized across delayed stages. The channel mapping exposes independent recurrences directly. The stage mapping introduces a scheduling delay and needs explicit validity and state handling. The custom four-lane mappings below are implementation derivations, not documented CMSIS-DSP APIs.

## Start from the public DF2T recurrence

For the positive-feedback convention used here, one DF2T stage computes `y = b0*x + d1`, then updates `d1_next = b1*x + a1*y + d2` and `d2_next = b2*x + a2*y`. Keep the old `d1` and `d2` values available until their final use. The public [CMSIS-DSP v1.18.0 F32 implementation](https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/FilteringFunctions/arm_biquad_cascade_df2T_f32.c), [initializer](https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/FilteringFunctions/arm_biquad_cascade_df2T_init_f32.c), and [stereo implementation](https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/FilteringFunctions/arm_biquad_cascade_stereo_df2T_f32.c) are the numerical, coefficient, and state-management references. In v1.18.0, its MVE path is gated by `ARM_MATH_MVEF && ARM_MATH_HELIUM_EXPERIMENTAL && !ARM_MATH_AUTOVECTORIZE`; this experimental path does not establish a generally available four-channel kernel. Check the exact source and build configuration used by the application.

## Map independent channels to vector lanes

A 128-bit MVE F32 vector holds four channel samples for one stage and one frame index. For channels 0 through 3, arrange `X=(x0,x1,x2,x3)`, `D1=(d1_0,d1_1,d1_2,d1_3)`, and `D2=(d2_0,d2_1,d2_2,d2_3)`. Apply the recurrence lane-wise:

```text
Y      = b0*X + D1
D1next = b1*X + a1*Y + D2
D2next = b2*X + a2*Y
```

If the filter design subtracts feedback, negate those feedback coefficients before applying these positive-feedback equations, and use coefficients that match the public API contract.

Each lane is an independent channel history, so this exposes real lane independence without substituting future outputs into the same stream. Keep each channel's state in its lane across frames and calls. If channels share a filter, broadcast the common scalar coefficients. If they use different filters, load coefficient vectors whose lane ordering matches the channel ordering. Do not shift coefficient or state identity when moving between channel groups.

For four contiguous frame-interleaved channels, one bounded full-vector load forms `X` and one full-vector store writes `Y`. For a wider frame, process explicit contiguous channel groups and account for the frame stride. Channel-major arrays place same-frame channel values in separate streams, so forming `X` generally needs gathers, lane assembly, or repacking unless the data is arranged as AoSoA. Include coefficient traffic, state movement, and any packing in the full-kernel assessment, and see [Helium memory access layout](techniques-helium-memory-access-layout.md) for general layout choices. For exact in-place frame-interleaved processing, load every valid channel in the current frame before storing outputs that could overwrite an input; respect the implemented API overlap contract for other buffer relationships.

For a partial final channel group, predicate the input loads and output stores and commit recurrence state only for valid channels. Do not let unused lanes affect another channel's state or access outside the buffer. Keep the frame count distinct from the number of scalar components used to represent a frame. See [Helium lane predicates and tail handling](techniques-helium-predication-and-tail-handling.md) for the memory and inactive-lane contracts.

Do not infer that enabling a Helium compiler target automatically selects a validated DF2T vector kernel. Inspect the generated implementation and verify its coefficient layout, state updates, tails, and numerical order. Use intrinsics or assembly when the maintained library/compiler path does not meet the implementation contract and the generated code or measured hotspot justifies a custom kernel.

## Pipeline cascade stages with delayed samples

A single-channel cascade can map four consecutive stages to four lanes by advancing each stage with a sample delayed by its position in the group. Let `Y_s[t]` be stage `s`'s output for original cascade sample `t-s`, where `s` is 0 through 3. At scheduler tick `t`, feed the stage lanes with `U[t]=(x[t],Y0[t-1],Y1[t-1],Y2[t-1])`; each lane runs its own DF2T recurrence using the coefficient set and `d1`/`d2` state belonging to that stage. After the tick, the output vector is `Y[t]=(Y0[t],Y1[t],Y2[t],Y3[t])`. The final cascade output in lane 3 corresponds to original sample `t-3`, so this mapping adds three ticks of output latency for a continuous stream. Shift sample values only: stage `s`'s coefficients and state always remain in lane `s`. Feeding a stage's current-tick result to the next stage in that same tick would restore the serial stage dependency and violate this delayed-sample schedule.

For the next tick, use the previous output vector as a whole-vector bit shift with the next scalar input as carry-in. `VSHLC #32` maps vector bits `(q0,q1,q2,q3)` and carry word `x_next` to `(x_next,q0,q1,q2)`, returning `q3` in scalar carry-out. Treat these as bit patterns: reinterpret F32 values as 32-bit unsigned words and back without numeric FP-to-integer conversion, and transfer the scalar carry with `memcpy` or a bit-cast. Explicitly set the carry input for every shift. The returned carry-out is lane 3; use it as the final cascade output only for a full four-stage group. For a partial stage group, select the output from the actual last valid stage lane instead of publishing `q3`. See the [ACLE MVE intrinsics reference](https://arm-software.github.io/acle/mve_intrinsics/mve.html) and [Helium whole-vector carry shifts](techniques-helium-multiword-carry-arithmetic.md).

For a finite block of `N` inputs, a stage-`s` sample is valid at tick `t` exactly when `0 <= t-s < N`. Return immediately for `N=0`, leaving all state unchanged. For `N>0`, run ticks `0` through `N+2` and emit lane 3 only when valid; this returns the `N` cascade outputs associated with input samples `0` through `N-1`. During startup and drain, do not update `d1` or `d2` for an invalid stage lane: preserve that lane's prior state, including nonzero initial state. Do not read `x[t]` when `t >= N`, and do not let a dummy lane value become a real input sample. Keep output order and the mapping between every delayed result and its source frame explicit.

At a block boundary, either drain the pending valid stage results while leaving stage 0 inactive, or retain the pending intermediate outputs and validity bookkeeping across calls and document the resulting streaming latency. A finite-block API that returns one result per input should drain without injecting extra real zero samples and return exactly `N` outputs. If the pipeline state is carried between chunks instead, keep every pending sample and its stage identity so the next chunk neither loses nor repeats work. For more than four stages, define and validate the group handoff, pending states, sample association, and API latency; do not restart each group independently or assume a universal tick skew. Partial groups with fewer than four stages or channels must preserve invalid-lane state and avoid invalid loads and stores.

For coefficients that vary with sample index, stage `s` must use the coefficient set for its actual sample `t-s`. Updating all lanes by scheduler tick can change the filter. Coefficient changes at a chunk boundary must also account for samples still in flight.

Check the schedule against a scalar cascade with nonzero initial `d1`/`d2` values, empty and one-sample blocks, partial channel/stage groups, block partitioning, coefficient changes, and exact output ordering. FMA grouping can change floating-point rounding, and shifting or buffering adds dependencies and storage; evaluate the complete kernel and its actual contract rather than inferring a benefit from lane count alone.
