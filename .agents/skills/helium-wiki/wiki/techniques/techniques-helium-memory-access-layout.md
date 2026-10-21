---
id: techniques-helium-memory-access-layout
title: Match Helium memory accesses to vector data layout
type: techniques
architectures:
  - armv8.1-m
tags:
  - helium
  - mve
  - vector-memory-layout
  - c
  - asm
confidence: source-reported
reproducibility: concept
related:
  - techniques-helium-vector-data-alignment
  - hardware-dtcm-scatter-gather-banks
  - techniques-cortex-m-memory-access-scheduling
  - techniques-cortex-m85-memory-access-scheduling
  - techniques-helium-instruction-interleaving
  - techniques-helium-predication-and-tail-handling
  - patterns-cmsis-dsp-helium-migration
sources:
  - https://support.arm.com/documentation/102095/0101/?lang=en
  - https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf
  - https://developer.arm.com/community/arm-research/b/articles/posts/making-helium-sudoku-registers-and-rabbits
  - https://developer.arm.com/community/arm-research/b/articles/posts/making-helium-going-around-in-circles
  - https://arm-software.github.io/acle/mve_intrinsics/mve.html
  - https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/FilteringFunctions/arm_fir_f32.c
aliases:
  - Helium byte and element pointer units
  - Helium contiguous and gather accesses
  - MVE gather and scatter offsets
  - Helium interleaved channel loads
  - MVE VLD2 and VLD4 layouts
  - Helium interleaved element widths
  - Helium vector memory layout
  - MVE size-changing loads
  - Helium memory pointer writeback
  - stereo FIR deinterleave and reinterleave
  - channel-aware interleaved stereo FIR layout
---

Use this guidance when choosing MVE load and store forms for sequential, irregular, widened, or interleaved data. Match address and element semantics to the layout so gather/scatter operations do not add unnecessary accesses, narrowing does not silently change numeric meaning, and multi-pattern channel transfers preserve the intended order and bounds.

## Keep element counts, byte sizes, and pointer steps separate

A 128-bit vector holds four F32 elements. With a `float` pointer, `p += 4` advances four elements, or 16 bytes; `p += 4 * sizeof(float)` advances sixteen elements (64 bytes when `sizeof(float) == 4`) because C pointer arithmetic already scales by the pointed-to type size. Keep vector lane counts, scalar element counts, byte lengths, and typed-pointer increments distinct. [Coding for Helium v1.1, §7, pp. 23–24](https://support.arm.com/documentation/102095/0101/?lang=en)

Document whether each public block-size argument counts bytes, scalar elements, or complex samples. If it counts bytes, validate and convert it to complete elements before deriving vector groups; if it counts elements, divide by the lane count. A full-vector-only loop needs an explicit divisible-length contract or a bounded remainder path; see [Helium lane predicates and tail handling](techniques-helium-predication-and-tail-handling.md). [Coding for Helium v1.1, §7, pp. 23–24](https://support.arm.com/documentation/102095/0101/?lang=en)

Inspect generated loads, stores, and address increments together, for example in compiler assembly output. In the Coding for Helium v1.1 guide's F32 multiplication example, each vector operation handles four 32-bit elements (16 bytes), while the generated code advances the pointers by 64 bytes. This exposes the sample's byte-count/typed-pointer unit error; do not copy its source or assembly as a recommended stride. For your own kernel, check that each effective address delta matches the elements transferred and the loop's element count. [Coding for Helium v1.1, §7, pp. 23–25](https://support.arm.com/documentation/102095/0101/?lang=en)

## Match the access form to the address pattern

Use contiguous vector loads and stores when consecutive lanes correspond to consecutive elements in memory. Gather and scatter support lane-dependent addresses, but one gather may require distinct memory accesses for its active elements. Do not infer contiguous-access behavior from the fact that a gather or scatter is encoded as one vector instruction. Keep core-specific timing and bank behavior separate; see [Cortex-M memory-access scheduling](techniques-cortex-m-memory-access-scheduling.md) and [TCM scatter/gather bank guidance](../hardware/hardware-dtcm-scatter-gather-banks.md). [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 105–108](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

Treat gather/scatter offsets as unsigned values bounded by the selected offset-lane representation. Distinguish unscaled byte offsets from forms that scale offsets by the accessed element size; a shifted word offset, for example, is scaled by the word size. A widening byte load into wider vector lanes can provide a wider offset representation, at the cost of fewer lanes per vector. Choose a width that covers the actual nonnegative offset range and verify the exact ACLE or assembly form. Do not rely on address-space wrap to manufacture negative offsets or treat that encoding as general C pointer permission. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 107–108](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

When widening a load, choose signed or unsigned extension to match the source values and subsequent arithmetic. A narrower store writes the selected output element width; it does not by itself apply the scaling, rounding, or saturation required by a numeric conversion. Perform the required shift or conversion explicitly, and preserve the algorithm's range and rounding contract. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 105–106](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

Loading packed narrow samples directly into wider lanes supplies arithmetic headroom without a separate expansion step. [Making Helium: Going around in circles (3/4)](https://developer.arm.com/community/arm-research/b/articles/posts/making-helium-going-around-in-circles)

## Keep predication and writeback semantics distinct

False-predicated load lanes become zero, while false-predicated stores leave their destination memory unchanged. Address writeback is a separate scalar effect and can still update the base even when vector lanes are inactive. Check both lane effects and pointer effects when using predicated memory operations. For remaining tail-state and bounds guidance, see [Helium lane predicates and tail handling](techniques-helium-predication-and-tail-handling.md). [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 105–108](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

When supported by the load/store form, pre/post-increment or base writeback can replace scalar pointer updates; inspect encoding and dependencies. [Making Helium: Going around in circles (3/4)](https://developer.arm.com/community/arm-research/b/articles/posts/making-helium-going-around-in-circles)

## Treat interleaved channel transfers as complete groups

`VLD2`/`VLD4` and `VST2`/`VST4` deinterleave or interleave patterns across two or four vector registers. A pattern instruction handles only part of the complete transfer; one `VLD2` or `VLD4` pattern is not an independent even-lane or odd-lane load. Use the complete matching pattern group for the intended stride. ACLE aggregate intrinsics such as `vld2q` and `vld4q` represent that full multi-instruction operation, not a single partial pattern. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 112–114, 142](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

For the MVE interleaved transfer design described here, two- and four-channel layouts support 8-, 16-, and 32-bit elements; select the channel count and width that match the data format. [Making Helium: Sudoku, registers and rabbits (2/4)](https://developer.arm.com/community/arm-research/b/articles/posts/making-helium-sudoku-registers-and-rabbits)

The four-way MVE store is divided among four pattern instructions, each transferring 128 bits, to balance memory work with ALU work. For target-specific scheduling, see [Helium instruction interleaving](techniques-helium-instruction-interleaving.md). [Making Helium: Sudoku, registers and rabbits (2/4)](https://developer.arm.com/community/arm-research/b/articles/posts/making-helium-sudoku-registers-and-rabbits)

For interleaved stereo or ARGB data, deinterleave only when the computation benefits from separate channel vectors, and interleave again if the output contract requires the original layout. Verify the component order and alpha placement against the caller's format. Aggregate channel loads consume a complete vector block and are not automatically safe for an incomplete final block; use a correctly bounded tail path, such as scalar cleanup or an explicitly tail-safe operation. Keep this separate from alignment and allocation requirements; see [vector data alignment](techniques-helium-vector-data-alignment.md). [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 112–114, 199–200, 202](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

For a stereo FIR built from mono library kernels, one complete `vld2q_f32` aggregate consumes four stereo frames, or eight interleaved scalar floats, and yields one four-float vector per channel. The corresponding `vst2q_f32` writes four frames in the same interleaved order. Advance the interleaved pointer by eight scalar elements and each mono-channel pointer by four elements; each mono filter processes the frame count for its channel, not the count of interleaved scalar components. The two channel vectors are a grouped transfer, not one ordinary vector load or store. Keep channel order explicit and handle any incomplete four-frame group with a bounded remainder. The ACLE aggregate definitions document these typed transfers. [ACLE MVE intrinsics](https://arm-software.github.io/acle/mve_intrinsics/mve.html)

The same operation can instead keep samples interleaved in a custom channel-aware kernel when that preserves separate channel histories, the intended tap mapping, and the output frame format. This is a design option, not a public CMSIS-DSP stereo-FIR API or a guaranteed improvement. Compare the conversion passes and temporary storage with the complete custom kernel, and retain two independent state histories even when the tap arrays are shared. See [CMSIS-DSP FIR integration](../patterns/patterns-cmsis-dsp-helium-migration.md) for the two-mono composition and state contracts.

The described patterns serve loads as well as stores and access pairs of contiguous 64-bit blocks. In systems with two interleaved 64-bit memory banks, the block addresses differ in bit 3, allowing parallel dispatch; check the target’s memory organization before relying on this opportunity. [Making Helium: Sudoku, registers and rabbits (2/4)](https://developer.arm.com/community/arm-research/b/articles/posts/making-helium-sudoku-registers-and-rabbits)

## Evaluate gathers used for lane permutations

In some kernels, a gather with a suitable offset vector can form a needed lane order directly from memory and avoid a sequence of scalar-register transfers for reordering. Include offset creation or loading, additional memory traffic, live-register pressure, and the target's memory-bank behavior in the decision; gather is not universally better than register rearrangement. Consult the target-specific bank guidance and verify the full kernel's memory layout before choosing the form. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 149–150, 209–210](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)
