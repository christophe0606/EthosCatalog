---
id: techniques-helium-matrix-kernel-layout
title: Reuse operands and map matrix layouts to Helium accesses
type: techniques
architectures:
  - armv8.1-m
tags:
  - helium
  - mve
  - matrix-kernel-layout
  - c
  - asm
  - half-precision
  - floating-point
confidence: source-reported
reproducibility: concept
related:
  - techniques-helium-memory-access-layout
  - techniques-helium-vector-reductions
  - techniques-helium-auto-vectorization
  - techniques-helium-instruction-interleaving
  - techniques-helium-predication-and-tail-handling
  - techniques-helium-low-overhead-loop-alignment
  - techniques-cortex-m-data-prefetch-and-cache-hints
  - patterns-helium-dtcm-dma-pipeline
  - hardware-dtcm-scatter-gather-banks
  - languages-arm-mve-intrinsics-and-assembly
  - patterns-cmsis-dsp-helium-migration
  - languages-c-float16-half-precision
  - techniques-profile-guided-helium-optimization
sources:
  - https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf
  - https://github.com/ARM-software/CMSIS-DSP/tree/v1.18.0/Source/MatrixFunctions
  - https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/MatrixFunctions/arm_mat_vec_mult_q15.c
  - https://arm-software.github.io/acle/mve_intrinsics/mve.html
  - https://arm-software.github.io/acle/main/acle.html#parallel-16-bit-multiplication
  - https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/MatrixFunctions/arm_mat_vec_mult_f16.c
  - https://documentation-service.arm.com/static/63190fc5ce77a54a32db3ac2
aliases:
  - MVE matrix row blocking
  - Helium matrix-vector row reuse
  - Helium matrix transpose layout
  - MVE 4x4 transpose VLD4
  - MVE 3x3 transpose scatter store
  - Helium s8 by s16 matrix-vector dot product
  - MVE packed matrix row-block traversal
  - Helium matrix-vector block-interleaved weights
  - Helium F16 dense matrix-vector row blocking
  - MVE F16 dense layer input reuse
  - Cortex-M55 dense weight refill demand
  - Helium packed byte and F16 weight formats
  - scalar DSP pre-expanded byte input fallback
prerequisites:
  - Confirm matrix dimensions, row/column layout, element type, numeric scaling, and source/destination overlap requirements.
  - Check the selected CMSIS-DSP release for a matching public matrix routine before maintaining a custom kernel.
---

Use this guidance when a small Helium matrix kernel is limited by repeated input loads or by a mismatch between matrix layout and vector accesses. Map the row-major storage to contiguous or structured vector operations, while preserving the kernel's accumulator, tail, and aliasing contracts.

## Map mixed signed-width matrix-vector dots to eight lanes

For a raw signed 8-bit matrix times signed 16-bit vector with signed 32-bit row outputs, preserve the contract y[r] = sum over c of sign_extend16(W[r,c]) * x[c]. This operation is matrix-vector multiplication. Keep it distinct from a quantized neural-network kernel that also applies input offsets, bias, requantization, rounding, or output saturation.

An MVE signed halfword dot step uses eight lanes: load eight s16 input values and load eight s8 weights with sign extension into s16 lanes. The step consumes 16 input bytes and 8 weight bytes, and the narrow matrix stays packed in memory instead of requiring a full expanded copy. Signed extension is required to preserve negative weights. ACLE names the 32-bit scalar dot accumulator vmladavaq_s16 (instruction VMLADAVA.S16); its long counterpart is vmlaldavaq_s16 (VMLALDAVA.S16) for a 64-bit scalar accumulator. These are horizontal dot reductions into a scalar accumulator. Bound the complete row sum and intermediate range before choosing 32-bit accumulation; use a wider accumulator when necessary, and check that the final result meets the declared output range. [Arm MVE Intrinsics Reference](https://arm-software.github.io/acle/mve_intrinsics/mve.html)

For each column block, load the input segment once, then reuse it while updating separate accumulators for a group of independent output rows. Keep the row remainder and column remainder as separate cases. For a short final column block, use bounded predicated loads plus inactive zero lanes or scalar cleanup; do not read beyond either object. Handle zero rows without touching output and, when the defined empty-column sum is used, write zero for each existing row. See [Helium lane predicates and tail handling](techniques-helium-predication-and-tail-handling.md) for predicate and loop-tail contracts.

Choose the row-group size from the live vector set, independent row accumulators, scalar pointer and loop state, and accumulator range. A larger group can improve input reuse and reduce row-pointer work, but can also increase register pressure and spills. Interleave independent loads and dot operations only when the target permits it, then inspect compiler output and the loop backedge on the selected core. Treat a schedule as target-specific; do not infer a universal unroll factor or performance result. See [Helium instruction interleaving](techniques-helium-instruction-interleaving.md), [MVE ACLE and assembly contracts](../languages/languages-arm-mve-intrinsics-and-assembly.md), and [Helium auto-vectorization](techniques-helium-auto-vectorization.md).

## Traverse packed row blocks as one weight stream

For a row group g, column block b, local row r, and lane k, define the packed weight order as P[g,b,r,k] = W[gU+r, bB+k], where B is the number of weights consumed by one dot step and k ranges from 0 to B-1. Flatten in the order g, b, r, k; with the eight-lane signed-halfword dot step above, B = 8. Thus one column block stores the eight weight bytes for row 0, then row 1, through the valid rows in the group; the next block follows those row segments. Reuse the same input segment across those row segments. A packed-weight pointer can advance through this stream instead of maintaining a separate matrix pointer for every row. If storage reserves slots for an incomplete final row group, allocate those slots explicitly and never load an invalid row.

This is row-block interleaving and traversal reordering; the logical operation remains y = W x. Define B as the number of logical weight elements consumed by one dot step, independently of the storage block size: a direct byte-element dot can use B = 16, while an eight-lane halfword operation uses B = 8. The existing signed-byte by signed-halfword operation also uses B = 8 because each weight is widened to a halfword lane. A 16-byte row segment holds one 16-element byte step or one eight-element F16 step. If a widening-byte kernel instead stores sixteen weights per row segment, consume that segment as two eight-lane steps using the corresponding two input halves, then advance to the next row segment. Load those input halves once for the row group when the live-register budget permits. This sixteen-weight storage grouping differs from the B = 8 packing order above; producer and consumer must agree on which column-block and row order the format uses.

Applying the same index identity to F16 is a derived custom packed layout; the public CMSIS-DSP [F16 matrix-vector routine](https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/MatrixFunctions/arm_mat_vec_mult_f16.c) uses row-major weights and does not consume that packed format.

A linear weight stream may simplify base-pointer state and improve sequential locality, including when weights reside in DTCM; it is not only a prefetch transformation. Evaluate placement and the actual cache or TCM path, and consider a DMA-to-DTCM pipeline where the system and reuse justify it. Pack static weights offline, or amortize online packing over enough calls to cover its transform work, scratch storage, and maintained layout contract. See [Cortex-M data prefetch and cache hints](techniques-cortex-m-data-prefetch-and-cache-hints.md), [DTCM and DMA pipeline](../patterns/patterns-helium-dtcm-dma-pipeline.md), and [DTCM bank access guidance](../hardware/hardware-dtcm-scatter-gather-banks.md).

The public CMSIS-DSP [Q15 matrix-vector implementation](https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/MatrixFunctions/arm_mat_vec_mult_q15.c) illustrates reusing one input vector across independent row accumulators. Its arithmetic contract is Q15-by-Q15 with 64-bit accumulation and a shifted, saturated Q15 output, so it is a row-reuse reference only; it is not an implementation of the raw signed 8-by-16 to signed 32-bit operation described here.

## Reuse vector inputs across independent rows

For an integer matrix-vector kernel with a matching scalar-result reduction, load a vector segment once and apply it to several independent matrix rows, keeping a separate scalar accumulator for each output row. This reuses the same input data across row calculations, but the number of rows processed together is a register and numeric-range decision: account for live matrix vectors, shared input vectors, accumulator width and bounds, and the actual memory path. Handle a row count that does not fill a complete row block and a column count that does not fill a vector separately. The Arm Helium reference book's four-row example illustrates reuse; it does not establish a universally optimal row blocking factor. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 183–185](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

For a quantized NN matrix kernel, reuse each loaded input vector across independent output-row dot products and interleave independent matrix loads and multiply-accumulates when the selected target permits it. Keep row accumulators within the chosen kernel's width and range, and preserve its input offsets, bias handling, row and column tails, and final output scaling. The Arm Helium reference book's load/MAC schedule is an implementation example; confirm dependencies and generated code on the target rather than assuming a universal schedule. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF p. 221](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

Floating-point row blocks also need independent accumulators for each output. Lane-wise partial sums require a final reduction, and changing their association must fit the numerical contract; see [Helium vector reductions](techniques-helium-vector-reductions.md).

## Match row reuse to weight delivery

Row blocking reduces repeated input reads; it does not reduce the number of matrix weights that must be fetched. When weights are used once from cacheable main memory, processing more rows per input segment can let the core consume weights faster and expose linefill supply as the limiting part of the loop. A schedule that works well with weights in TCM may behave differently when those weights stream through the cache and external memory.

Choose the row group with the real memory path and a representative workload. Inspect core load and multiply-accumulate scheduling, live registers and spills, weight linefill demand, and prefetch coverage as separate factors. A smaller group can be appropriate when memory delivery cannot sustain a larger group; no one group size is optimal across placements. See [Cortex-M data prefetch and cache hints](techniques-cortex-m-data-prefetch-and-cache-hints.md) and [profile-guided Helium optimization](techniques-profile-guided-helium-optimization.md).

## Optional scalar DSP fallback with pre-expanded inputs

On a target with the Arm DSP paired signed-halfword multiply-accumulate operation, a scalar fallback can keep matrix weights as signed bytes and convert a reusable input vector to signed halfwords once. Pair adjacent weights and inputs in the same order, sign-extend each byte correctly, and use the paired operation only when the target supports its ACLE interface. The ACLE `__smlad` operation adds two signed halfword products to a 32-bit accumulator without saturation, so prove the accumulator range or use a wider accumulation strategy. Do not treat byte selection or packing helpers as an automatic adjacent-pair conversion. [ACLE, Parallel 16-bit multiplication](https://arm-software.github.io/acle/main/acle.html#parallel-16-bit-multiplication)

Pre-expansion moves conversion work out of repeated row dot products, but it adds setup work, workspace, and input traffic; the expanded input uses twice the storage of the byte input. Include those costs when comparing the fallback with direct MVE processing, and retain it only when reuse and the memory path justify its representation. Preserve pair order, input offsets and scaling, odd-column cleanup, zero-length behavior, and the selected operation’s arithmetic range. This is an optional custom fallback format, not a CMSIS-DSP ABI or a replacement for a quantized operator’s numerical contract.

## Reuse F16 inputs across dense output rows

Use this mapping for a row-major F16 dense matrix-vector layer, y[r] = b[r] + sum_c(W[r,c] * x[c]). Eight half-precision values fit an MVE vector. For each input segment, load x once, then update separate lane-wise accumulators from the corresponding contiguous weight segment of several output rows. This shares input traffic while keeping the original row-major matrix; it is row blocking, distinct from unrolling consecutive column iterations. Repeated input loads compete with weight loads for memory-transfer resources on the selected Cortex-M55 path. Reusing the input and interleaving independent weight loads with row arithmetic can ease that demand; confirm overlap and dependencies on the actual core. See [MVE ACLE](https://arm-software.github.io/acle/mve_intrinsics/mve.html) and [F16 matrix-vector row reduction](techniques-helium-vector-reductions.md).

Handle output-row remainders separately from a partial input vector so no nonexistent row or bias is read. Zero output rows require no output access; for the defined empty-column sum, zero input columns produce bias-only outputs. In a custom column tail, bound both input and weight accesses and exclude invalid products with predicated arithmetic or scalar cleanup. See [Helium tail handling](techniques-helium-predication-and-tail-handling.md).

Reduce each row's eight partial sums after all columns have contributed, add that row's bias once, and then store. The F16 fused multiply-accumulate and lane reduction can round differently from a scalar left-to-right loop, so define the accepted error and intermediate range. See [F16 finalization and invariant work](../patterns/patterns-helium-small-kernel-fusion.md).

Choose the row-group size from the complete live set: shared input and weight vectors, one accumulator per active row, reduction temporaries, row pointers, loop state, bias values, ABI obligations, and spills. A larger group can reuse x more, but does not guarantee a better schedule. The public [CMSIS-DSP v1.18.0 F16 matrix-vector implementation](https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/MatrixFunctions/arm_mat_vec_mult_f16.c) uses a four-row main group and separate smaller row remainders as one maintained example. Audit its column-tail contract before reuse: the tail predicates the input load but still reads full weight vectors and performs unpredicated arithmetic. Ensure those accesses are valid and padded values meet the numeric contract, or use a bounded tail path. See [MVE F16 register budgeting](../languages/languages-arm-mve-intrinsics-and-assembly.md).

## Preserve contiguous traversal or amortize packing

Keep the innermost traversal over contiguous row elements where the layout permits it. In matrix-matrix work, one operand may otherwise be visited by columns with a strided access pattern; consider tiling or packing that operand into a more contiguous working layout only when later reuse justifies the extra copy, storage, and setup. The useful tile depends on the memory hierarchy and active working set, so compare the complete access pattern rather than assuming a packed copy is always beneficial. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF p. 175](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

For convolution, HWC can make each pixel's channel values contiguous for a particular patch-generation and load pattern. Match the layout and any weight reordering to the selected operator's exact dimension and stride contract; do not impose HWC on unrelated NN operators. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 221, 227–228](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

When a full convolution im2col expansion would exceed the scratch budget, expand a small reusable group of patches and interleave patch generation with matrix computation. Choose the group size from scratch demand, input reuse, and the selected matrix kernel's contract. The Arm Helium reference book's two-column grouping is one historical example, not a universal block size. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 220–221](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

## Map complete fixed-size transpose blocks to vector memory operations

For a complete row-major 4×4 block of 32-bit elements, a VLD4 deinterleave can place each original matrix column into a vector; four contiguous vector stores then write those columns as rows of the transposed output. Treat an aggregate ACLE VLD4 intrinsic as the complete four-pattern transfer it represents, not as one isolated pattern instruction. Use this form only when the full block is valid and the source, destination, and output stride match the intended layout; arrange a separate bounded path for incomplete blocks. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 185–186](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

For a row-major 3×3 matrix of 32-bit elements, two contiguous four-element loads cover the first eight values; scatter stores can place them at their transposed output positions, with the ninth value handled separately. Confirm whether the chosen scatter form interprets offsets as byte offsets or scaled element offsets, and keep offsets within the intended destination object. This describes an out-of-place mapping; it does not by itself make overlapping input and output safe. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF p. 186](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

## Select fixed-size routines by their complete contract

Small fixed-dimension routines can use a different implementation from a generic matrix loop. Before selecting a library specialization or writing a dedicated kernel, match the exact dimensions and layout, element type and signedness, accumulator range, fixed-point scaling and final saturation or rounding, and any row or column remainder handling. Check the selected public CMSIS-DSP release for the function and its documented input/output overlap requirements; do not infer a current library contract from a historical example. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 185–186; CMSIS-DSP v1.18.0 MatrixFunctions](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

## Protect unread values when buffers overlap

The transpose layouts above assume distinct source and destination storage. For an in-place transpose or any overlapping buffers, preserve every input value until it has been read. Prove that the selected load/store order does not overwrite an unread element, or stage the complete affected tile in temporary storage; do not infer arbitrary in-place safety from an out-of-place gather/scatter or VLD4 example. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 185–186](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)
