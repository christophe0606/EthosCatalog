---
id: languages-arm-mve-intrinsics-and-assembly
title: Preserve ACLE, ABI and context contracts in MVE code
type: languages
architectures:
  - armv8.1-m
tags:
  - helium
  - mve
  - mve-programming-contracts
  - floating-point
  - c
  - cpp
  - asm
  - half-precision
confidence: source-reported
reproducibility: concept
related:
  - techniques-arm-compiler-mve-target-configuration
  - techniques-helium-predication-and-tail-handling
  - techniques-helium-instruction-interleaving
  - techniques-helium-matrix-kernel-layout
  - techniques-helium-vector-reductions
  - patterns-cmsis-dsp-helium-migration
  - techniques-helium-auto-vectorization
  - techniques-helium-low-overhead-loop-alignment
  - languages-c-float16-half-precision
sources:
  - https://support.arm.com/documentation/102095/0101/?lang=en
  - https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf
  - https://developer.arm.com/architectures/system-architectures/software-standards/acle
  - https://developer.arm.com/community/arm-research/b/articles/posts/making-helium-why-not-just-add-neon
  - https://github.com/llvm/llvm-project/blob/main/llvm/lib/Target/ARM/ARMInstrMVE.td
  - https://github.com/ARM-software/abi-aa/blob/main/aapcs32/aapcs32.rst
  - https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/MatrixFunctions/arm_mat_vec_mult_f16.c
aliases:
  - __ARM_MVE_PRESERVE_USER_NAMESPACE
  - MVE inline assembly clobbers
  - Arm MVE ACLE intrinsics
  - Helium inline assembly contracts
  - MVE register and ABI rules
  - Helium exception context
  - arm_mve.h vector types
  - MVE ECI exception continuation
  - MVE scalar register constraints
  - Helium widening-load base registers
  - MVE F16 Q-register budget
  - Helium F16 row accumulator register pressure
---

Use these rules when implementing Helium code with C/C++ intrinsics, inline assembly, or callable assembly functions. They keep scalar-reference behavior, compiler assumptions, ABI obligations, and exception continuation state aligned so that low-level optimization does not silently break callers or interrupt handling.

## Progress from a reference implementation to intrinsics

Begin with portable scalar code that can serve as a correctness reference. Profile it to find the actual bottleneck, then check whether a matching library routine or compiler auto-vectorization addresses that hotspot before writing intrinsics. Intrinsics provide explicit MVE operations in C/C++ while leaving register allocation and pipeline scheduling to the compiler. Their source can be easier to port than hand-scheduled assembly, but still requires compatible target features, matching semantics, and generated-code inspection. Use assembly when compiler output does not meet the kernel's instruction-selection or scheduling needs and the target-specific schedule can be maintained. Isolate architecture-specific code in separate files or functions where that helps maintenance and porting; keep scalar control code separate when it makes the vector contract easier to review. [Coding for Helium v1.1, §§3, 7, pp. 9–10, 21](https://support.arm.com/documentation/102095/0101/?lang=en) [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 151–152](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

## Guard the intrinsic header and namespace

Compile intrinsic-using code only for a target with the required `__ARM_FEATURE_MVE` capability, and include `arm_mve.h` in each source file that uses intrinsics. By default, the header exposes both ordinary intrinsic names and their `__arm_`-prefixed forms. Define `__ARM_MVE_PRESERVE_USER_NAMESPACE` before including it to hide the ordinary names and avoid collisions. [Coding for Helium v1.1, §7, pp. 21–22](https://support.arm.com/documentation/102095/0101/?lang=en)

Explicit intrinsics and compiler vectorization can coexist: an MVE-capable target and the intrinsic header enable explicit operations, while optimization settings can separately expose independent ordinary C/C++ code to auto-vectorization. Auto-vectorization is not a prerequisite for intrinsic calls to lower to MVE instructions. [Coding for Helium v1.1, §7, p. 22](https://support.arm.com/documentation/102095/0101/?lang=en)

## Use ACLE types and distinguish conversion from reinterpretation

Use the ACLE interfaces declared through `arm_mve.h` and the corresponding vector types to express MVE operations in C or C++. `vreinterpret` changes the compiler's type view while preserving the bits; it does not numerically convert the value. `vcvt` performs a numeric conversion and may change precision or rounding. Use the operation that matches the data contract. Compiler-specific vector operators or indexing syntax are not necessarily portable ACLE, so verify them against the selected compiler and ACLE version. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 37, 141–142, 150](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

An intrinsic is an interface, not a promise of one machine instruction. A predicated intrinsic can require predicate setup plus the operation, and an aggregate `vld4q` represents the complete sequence of VLD4 pattern instructions. Inspect generated assembly for the actual sequence, address updates, predicates, and live-register use; confirm that a source-level operation preserves the intended lane and memory behavior. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 140–144](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

## Account for shared vector and scalar register storage

The eight 128-bit Q registers alias the scalar floating-point S and D registers, so scalar FP temporaries can consume the same physical storage as live MVE vectors. Keep the number of simultaneously live vectors within the available register set and inspect compiler output for spills. Where an instruction supports a vector/scalar operand form, using a scalar general-purpose register can avoid an unnecessary vector duplicate. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 43–44, 151](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

## Budget scalar register classes for mixed-width row blocks

When a hand-scheduled matrix loop keeps several output rows active, count general-purpose registers as well as the eight MVE vector registers. For the signed halfword reduction used here, the VMLADAVA.S16 32-bit scalar accumulator is allocated to an even-numbered GPR in the corresponding LLVM MVE encoding. The signed long reduction uses an even/odd GPR pair, so a 64-bit accumulator consumes two GPRs per output row and can reduce the practical row-block size. Check the exact selected instruction and compiler version when lowering the intrinsic. [LLVM ARM MVE instruction definitions](https://github.com/llvm/llvm-project/blob/main/llvm/lib/Target/ARM/ARMInstrMVE.td) [Arm MVE Intrinsics Reference](https://arm-software.github.io/acle/mve_intrinsics/mve.html)

Address-register classes can constrain the same loop. The contiguous signed byte-to-halfword VLDRB.S16 widening-load form uses a low GPR base (r0-r7) in the immediate addressing encoding. This is a form-specific constraint, not a property of every vector load. Confirm that the selected encoding applies and inspect emitted code. Keeping a separate weight pointer per row can exhaust this constrained class quickly. The packed traversal in [Helium matrix kernel layout](../techniques/techniques-helium-matrix-kernel-layout.md) advances one weight pointer across row segments and can reduce that base-pointer demand.

Include input and output pointers, row accumulators, loop controls, temporary addresses, ABI-preserved registers, and any compiler-generated spills in the complete allocation. A low-overhead loop may keep LR live as loop state, and LR also carries the function return address at entry; preserve the applicable calling convention before using it for another purpose. Recheck allocation when changing accumulator width, unroll or row-block size, or surrounding code. Treat row limits as properties of the exact implementation and ABI, not fixed MVE maxima. For accumulator-range choices see [Helium vector reductions](../techniques/techniques-helium-vector-reductions.md). [AAPCS32](https://github.com/ARM-software/abi-aa/blob/main/aapcs32/aapcs32.rst)

## Budget Q registers for F16 row accumulators

A blocked F16 dot-product loop can keep one shared input vector, one current weight vector, and one lane-wise accumulator for each active output row. MVE exposes eight Q registers, so this simplified live-set count leaves at most six row accumulators when both shared vectors are live. It is only a ceiling for that loop body: reduction temporaries, predicates, bias or scalar values, saved state, and compiler allocation can lower the practical group size. F16 scalar S/D values share storage with Q registers, so account for that aliasing when inspecting generated code.

The vector-register count is only part of the budget. Separate weight pointers for each row, input/output/bias pointers, dimensions, loop counters, and low-overhead-loop state consume general-purpose registers and interact with the applicable ABI. F16 VFMA accumulates lane-wise partial sums in Q registers and still needs a final reduction; this differs from integer dot forms that reduce into a GPR accumulator. The maintained [CMSIS-DSP F16 matrix-vector implementation](https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/MatrixFunctions/arm_mat_vec_mult_f16.c) provides a smaller row-group reference. Inspect allocation and spills for the selected compiler and target rather than treating the six-row ceiling as an optimum.

If handwritten assembly is justified, preserve the complete function contract: initialize every accumulator, guard zero work, use the correct halfword count, align the actual loop target, expose post-incremented pointers as read/write, declare changed registers and memory effects, preserve ABI-required state, and perform reductions, bias, stores, and remainders. Follow [Helium loop alignment](../techniques/techniques-helium-low-overhead-loop-alignment.md) and [tail handling](../techniques/techniques-helium-predication-and-tail-handling.md); an isolated loop body is not a complete callable implementation.

## Make inline-assembly effects visible to the compiler

Model each inline-assembly value's data flow explicitly: input operands describe consumed values, output operands describe overwritten results, and read/write operands describe values both consumed and changed. Also declare early-clobber outputs, fixed-register requirements, condition-code changes, and memory side effects. If an instruction updates a pointer, declare it as both read and written so the compiler cannot reuse its stale value. Prefer symbolic operands and compiler-selected registers when possible. Constraint codes and immediate ranges vary by toolchain and instruction; verify them against the selected compiler rather than copying unverified constraint strings. [Coding for Helium v1.1, §8, pp. 26–27](https://support.arm.com/documentation/102095/0101/?lang=en) [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 152–155](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

In the Coding for Helium v1.1 guide's GNU-style inline-assembly form, the post-incremented source pointers are read/write operands (`+r`); keep this update visible to the compiler, but do not assume that constraint spelling is universal. The complex dot-product example also writes Q0 and Q1 without declaring those vector-register effects. Do not copy that assembly as safe: Q registers alias scalar floating-point registers, so undeclared writes can corrupt values the compiler keeps live. Declare every changed register using constraints or clobbers supported by the selected compiler, or use compiler-allocated vector outputs. `volatile` does not repair missing inputs, outputs, or clobbers. [Coding for Helium v1.1, §8, pp. 26–27, 29, 31](https://support.arm.com/documentation/102095/0101/?lang=en) [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 43–44](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

## Preserve callable-function and enclosing-function ABI obligations

Follow the selected Arm procedure-call standard and ABI variant for native assembly functions, including scalar general-purpose registers and stack alignment. The Arm Helium reference book summarizes Q4–Q7 as callee-preserved and Q0–Q3 as caller-saved and available for arguments or results; it also says the VPR mask fields must be zero at function boundaries while the P0 predicate bits need not be preserved. Treat that summary as an orientation, not a complete ABI specification, and verify the exact obligations for the selected toolchain and environment. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF p. 156](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf); [Procedure Call Standard for the Arm Architecture (AAPCS32)](https://github.com/ARM-software/abi-aa/blob/main/aapcs32/aapcs32.rst)

Do not remove required callee saves when moving a callable kernel into a loop or combining it with a larger function. Saves may be placed around a larger enclosing region to amortize repeated entry/exit work only when the complete enclosing function still preserves every value required by its caller. Check the register liveness and all exits across that full scope. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 156, 210](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

## Preserve MVE exception context and stack capacity

MVE instructions execute in beats, so an exception can occur while vector instructions are partially complete. Preserve the architecturally defined exception continuation state when returning to interrupted code; do not treat every vector instruction as indivisible or reconstruct continuation from undocumented field assumptions. Exception context can include the shared FP/MVE register state and VPR according to the applicable frame and system policy. Context-switch and handler code must retain that state when it owns exception return. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 43–44, 159–162](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

The ECI state records completed MVE beats. On exception return, those beats are skipped rather than rolled back or repeated; preserve this state when handler or context-switch code owns the return. [Making Helium: Why not just add Neon? (1/4)](https://developer.arm.com/community/arm-research/b/articles/posts/making-helium-why-not-just-add-neon)

With lazy FP/MVE context preservation, the system can reserve extended-frame space and defer saving the shared register state until an exception handler executes an FP or MVE instruction. This can avoid that state transfer when the handler does not use those features, while the first such instruction can trigger preservation. Audit latency-sensitive handlers for compiler-generated FP/MVE instructions as well as explicit intrinsics, and budget stack capacity for the applicable eager or lazy context policy. Follow the system's documented exception policy; do not assume every handler avoids FP/MVE or claim a fixed interrupt cost. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 159–160](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)
