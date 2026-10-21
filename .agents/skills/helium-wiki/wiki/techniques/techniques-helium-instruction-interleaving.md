---
id: techniques-helium-instruction-interleaving
title: Interleave Helium instructions on Cortex-M52, Cortex-M55, and Cortex-M85
type: techniques
architectures:
  - cortex-m52
  - cortex-m55
  - cortex-m85
tags:
  - instruction-scheduling
  - helium
  - mve
  - asm
confidence: source-reported
reproducibility: concept
related:
  - techniques-helium-auto-vectorization
  - techniques-helium-low-overhead-loop-alignment
  - techniques-cortex-m-branch-prediction
  - techniques-cortex-m-latency-aware-scheduling
  - techniques-cortex-m-floating-point-scheduling
  - techniques-cortex-m-memory-access-scheduling
  - techniques-cortex-m85-latency-aware-scheduling
  - techniques-cortex-m85-memory-access-scheduling
  - techniques-cortex-m85-floating-point-scheduling
  - techniques-helium-fir-tap-blocking
  - techniques-helium-df1-iir-vectorization
  - hardware-dtcm-scatter-gather-banks
  - techniques-profile-guided-helium-optimization
  - patterns-helium-small-kernel-fusion
  - techniques-helium-vector-reductions
  - techniques-helium-predication-and-tail-handling
  - techniques-helium-df2t-lane-mapping
sources:
  - https://developer.arm.com/community/arm-community-blogs/b/architectures-and-processors-blog/posts/armv8_2d00_m-based-processor-software-development-hints-and-tips
  - https://developer.arm.com/community/arm-research/b/articles/posts/making-helium-why-not-just-add-neon
  - https://developer.arm.com/community/arm-research/b/articles/posts/making-helium-bringing-amdahl-s-law-to-heel
  - https://support.arm.com/documentation/102692/0101
  - https://support.arm.com/documentation/107950/0100
  - https://support.arm.com/documentation/107730/0003
  - https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/FilteringFunctions/arm_fir_f32.c
  - https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/FilteringFunctions/arm_fir_q15.c
  - https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/FilteringFunctions/arm_biquad_cascade_df1_f32.c
  - https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/FilteringFunctions/arm_biquad_cascade_df2T_f32.c
  - https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/FilteringFunctions/arm_biquad_cascade_df2T_init_f32.c
  - https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/FilteringFunctions/arm_biquad_cascade_df1_f16.c
  - https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/ComplexMathFunctions/arm_cmplx_mult_cmplx_f32.c
  - https://arm-software.github.io/acle/mve_intrinsics/mve.html
  - https://github.com/Arm-Examples/Helium-Optimization/blob/bc9dfb67be64a2a5ee8e5325910a3f060a593863/Performance_analysis/ml-dsp-front-end-optimization-with-helium.md#L354-L459
  - https://github.com/Arm-Examples/Helium-Optimization/blob/bc9dfb67be64a2a5ee8e5325910a3f060a593863/General_optimizations/optimizing_c_functions.md#L4-L23
  - https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf
aliases:
  - Helium instruction scheduling
  - MVE instruction interleaving
  - instruction overlap
  - assembly scheduling
  - Cortex-M55 MVE two-tick overlap
  - M55 E2 E3 SRF write hazards
  - Cortex-M55 I F LS SY pipeline scheduling
  - Cortex-M85 MVE overlap
  - Cortex-M85 SIMD instruction groups
  - Cortex-M85 load store overlap parity
  - M55 two-vector gain scheduling
  - Helium two-vector complex load schedule
  - Cortex-M52 one-beat MVE overlap
  - Cortex-M52 shared VF pipeline
  - Cortex-M52 MVE tick scheduling
  - MVE operand reuse across independent outputs
  - LLVM-MCA Helium scheduling diagnosis
  - dependent MVE load/MAC overlap
  - cross-iteration load and MAC overlap
  - loop backedge scheduling
  - recursive biquad register scheduling
  - MVE recursive feedback register rotation
  - stream biquad coefficient vectors
  - recursive kernel live vector budget
evidence_basis: "Processor pipeline and instruction characteristics are source-reported in the Cortex-M52 r0p3, Cortex-M55 r1p1, and Cortex-M85 r1p0 Processor Software Optimization Guides. CMSIS-DSP v1.18.0 links corroborate FIR and biquad implementation examples. The fourth-order recurrence register-footprint example is independently derived; ACLE documents its vector operations and lane transfers. The recursive biquad register-role discussion is an independently derived scheduling concept and does not replace the source-reported core rules."
---

Use this page when scheduling MVE instructions on Cortex-M52 r0p3, Cortex-M55 r1p1, or Cortex-M85 r1p0. Each core has different tick models, execution pipes, and overlap limits; follow the matching core-specific section and preserve dependencies. The source-reported schedules describe processor behavior; they do not predict application performance by themselves.

## Cortex-M55 r1p1 two-tick scheduling model

For Cortex-M55 r1p1 (Arm Cortex-M55 Processor Software Optimization Guide r1p1, Issue 03), a 128-bit MVE vector instruction is processed as two 64-bit micro-operations called ticks; each tick executes two 32-bit beats. Tick 1 of an older instruction can execute alongside tick 0 of a newer instruction, and the timing tables describe overlap for up to two beatwise MVE instructions. The timing tables define latency as the minimum delay seen by a dependent operation and throughput as the maximum instruction rate for a group; neither is an application loop cycle count. [Cortex-M55 r1p1 optimization guide, §2.2, pp. 11–15; §3.1, p. 16; §4.1, p. 46](https://support.arm.com/documentation/102692/0101)

Use the utilized-pipeline labels from the exact instruction row: `I` is the Integer Vector pipe, `F` the Floating Point pipe, `LS` the Load/Store pipe, and `SY` the System Registers pipe. MVE instructions are decoded in E0, and the EPU runs in lock-step with the DPU. E0 checks resource use and data availability; if a hazard stalls E0, the overlap does not happen. In general, the older instruction's tick 1 and the newer instruction's tick 0 must use different pipes. [Cortex-M55 r1p1 optimization guide, §2.2, pp. 14–15; §3.1, p. 16; §4.1, p. 46](https://support.arm.com/documentation/102692/0101)

### Use each encoding's pipeline assignment

| Instruction examples | Source-reported pipeline and timing | Scheduling meaning |
| --- | --- | --- |
| Many integer vector arithmetic, bitwise, shift, move, duplicate, negate, and select groups | Usually `I`; many listed groups have latency 1 and throughput 1/2. | Integer-vector instructions do not all share the same pipe; check exceptions below. |
| Integer vector compare (`VCMP`, including its VPT forms) and integer vector multiply groups | `F`; multiply rows include latency 2 and throughput 1/2. | Integer data type alone does not imply `I`. |
| Floating-point vector arithmetic and multiply groups | `F`; listed common arithmetic and multiply rows include latency 2 and throughput 1/2. | Check forwarding and following-instruction restrictions for FP compare/VPT rows. |
| Scalar-lane `VMOV` from a general-purpose register to a vector lane | `I/F`; the note says issue to `I` if available, otherwise to `F`; latency 1, throughput 1. | Its pipe can depend on availability, so do not assume a fixed pipe when scheduling a following operation. |
| Scalar-lane `VMOV` from a vector lane to a general-purpose register | `F`; latency 1, throughput 1; the table points to the E2 SRF write-port hazard. | Account for its scalar-register write stage as well as its pipe. |
| Continuous vector loads and stores, `VLD2`/`VLD4`, and `VST2`/`VST4` | `LS`. Continuous loads/stores commonly have latency 1, throughput 1/2; `VLD2`/`VLD4` have latency 2, throughput 1/2. | A memory instruction's actual form determines its pipe and timing. |
| Some gather-load and scatter-store variants | `I & LS` for the listed encodings, including gather-load `T5`/`T6` and scatter-store `T5`/`T6`. | These forms use two pipes; use only the mixed-pipe ordering rule that the [Cortex-M55 r1p1 optimization guide](https://support.arm.com/documentation/102692/0101) gives for the gather-load rows. |
| `VCTP`, vector system-register transfers, `VMRS`/`VMSR`, `VPNOT`, and `VPST` | `SY`; latency 1, throughput 1. | The [Cortex-M55 r1p1 optimization guide](https://support.arm.com/documentation/102692/0101) explicitly says instructions using `SY` can overlap with themselves, an exception to the general different-pipe rule. |
| `VABAV` | `I & F`; latency 5, throughput 1/4. Its first cycle is in `I`, second in `F`. | Follow the specific mixed-pipe ordering note below. |

These are representative groups, not a replacement for the latency-table row: utilization can differ by instruction variant. The Cortex-M55 r1p1 optimization guide also gives element-size-dependent timings for reductions and mixed-pipe memory variants. [Cortex-M55 r1p1 optimization guide, §3.10, Tables 3-16, pp. 34–40; §3.11, Table 3-17, pp. 40–41; §3.12, Table 3-18, pp. 42–43; §3.13, Table 3-19, pp. 43–44; §§3.14–3.15, Tables 3-20–3-21, pp. 44–45](https://support.arm.com/documentation/102692/0101)

### Scalar instructions that may overlap

The Cortex-M55 r1p1 optimization guide lists scalar classes that are allowed to overlap tick 1 of the preceding vector instruction: immediate branches (`B`, `BL`, `CB[N]Z`), low-overhead-loop instructions, Branch Future instructions (which are NOPs), and integer arithmetic. From the integer-arithmetic class, it excludes divide, every `CSEL` form, MVE scalar shifts, and PC-modifying instructions. Loads and stores can also overlap, except load/store multiple and PC-modifying forms. “Allowed” is conditional on hazards and does not guarantee overlap in a particular sequence. [Cortex-M55 r1p1 optimization guide, §4.1, p. 46](https://support.arm.com/documentation/102692/0101)

The Cortex-M55 r1p1 optimization guide also names limits: arithmetic cannot overlap a vector load/store that writes its base back to a scalar register; an instruction that checks the stack limit does not overlap; a scalar cannot overlap a vector instruction carrying an implicit `LE` (the final instruction in a low-overhead loop); and a scalar/vector dependency makes overlap unlikely. A floating-point VPT row separately warns that it cannot overlap a following `I`-pipe instruction because there is no `F`-to-`I` forwarding. [Cortex-M55 r1p1 optimization guide, §4.1, p. 46; §3.12, Table 3-18, pp. 42–43](https://support.arm.com/documentation/102692/0101)

For EPU data dependencies, results from E2 and E3 can be forwarded to following instructions of the same class. Forwarding is not supported between different classes, including floating-point to fixed-point operations and vector to scalar floating-point operations. Resource overlap and data forwarding are separate checks: a different-pipe pair can still be blocked by a dependency. [Cortex-M55 r1p1 optimization guide, §2.2, p. 14](https://support.arm.com/documentation/102692/0101)

### Preserve the mixed-pipeline ordering notes

For `VABAV`, the first cycle uses `I` and the second uses `F`. The Cortex-M55 r1p1 optimization guide says fully overlapped execution requires the older instruction to use a non-`I` pipe and the younger instruction to use a non-`F` pipe. For gather-load `T5`/`T6` rows using `I & LS`, the first cycle is in `LS` and the second in `I`; the corresponding condition is older instruction on a non-`LS` pipe and younger instruction on a non-`I` pipe. These are specific ordering rules for the noted instruction groups, not a general recipe for all mixed-pipe instructions. [Cortex-M55 r1p1 optimization guide, §3.10, Table 3-16, p. 40; §3.14, Table 3-20, p. 44](https://support.arm.com/documentation/102692/0101)

The gather-load and store footnotes are not interchangeable: the exact forwarding and dependency effects are listed below. In particular, do not transfer the gather-load latency penalty to stores or other gather encodings. [Cortex-M55 r1p1 optimization guide, §3.14, Table 3-20, p. 44; §3.15, Table 3-21, p. 45](https://support.arm.com/documentation/102692/0101)

The memory rows distinguish several encodings that look similar at source level:

| MVE memory form | Source-reported timing and pipeline |
| --- | --- |
| Gather load `T1` | Latency `(64 / esize) + 1`, throughput `esize / 64`, `LS`; its note adds 4 cycles to the preceding instruction latency because this form cannot read forwarded data. |
| Gather load `T2`–`T4` | Latency `(64 / esize) + 1`, throughput `esize / 64`, `LS`. |
| Gather load `T5` | Latency `1(2)`, throughput 1/2, `I & LS`; first cycle `LS`, second `I`. |
| Gather load `T6` | Latency 2, throughput 1/4, `I & LS`; first cycle `LS`, second `I`. |
| Scatter store `T1`–`T4` | Latency 1, throughput `esize / 64`, `LS`. |
| Scatter store `T5` / `T6` | Latency 2, throughput 1/2 / 1/4 respectively, `I & LS`. |

The store rows state that dependent store data does not change the preceding instruction's latency. The gather-load pair ordering note applies to `T5`/`T6`; the store table does not give the same LS-then-I ordering rule for its `I & LS` variants. [Cortex-M55 r1p1 optimization guide, §3.14, Table 3-20, p. 44; §3.15, Table 3-21, p. 45](https://support.arm.com/documentation/102692/0101)

### Avoid same-stage scalar-register write conflicts

Two instructions that write the Scalar Register File (SRF) in E2 cannot overlap each other in E2; the same applies to two E3 SRF writers. The latency tables point to E2 for vector-lane-to-general-register `VMOV` and floating-point min/max reductions, and to E3 for integer reductions and indexed duplicate operations. When a listed instruction carries the E2 or E3 note, retain that restriction even if its utilized pipeline differs from its neighbor's. [Cortex-M55 r1p1 optimization guide, §4.1.1–4.1.2, p. 47; §3.11, Table 3-17, pp. 40–41; §3.12, Table 3-18, pp. 42–43; §3.10, Table 3-16, pp. 34, 37, 40](https://support.arm.com/documentation/102692/0101)

For the listed integer and floating-point vector min/max reductions, the table gives latency `(64 / esize) + 1` and throughput `esize / 64`. Use only the element sizes supported by the selected instruction variant: the formula gives latency 9 and throughput 1/8 for an 8-bit variant, 5 and 1/4 for a 16-bit variant, and 3 and 1/2 for a 32-bit variant. This does not imply that every reduction supports all three sizes. The integer arithmetic-to-scalar row separately prints latency `3(2)` and throughput 1/2; its footnote points to the E3 write hazard but does not explain the parenthetical latency value. Preserve that ambiguity instead of assigning a condition to the `2`. [Cortex-M55 r1p1 optimization guide, §3.10, Table 3-16, pp. 34, 40–41; §3.12, Table 3-18, pp. 42–43](https://support.arm.com/documentation/102692/0101)

The MVE scalar-shift rows report latency `2(5)` and throughput `1(1/4)`. Their note says that with `CPACR.CP10 != 0b00`, execution uses the EPU with shorter latency and higher throughput; when `CPACR.CP10 == 0b00`, execution uses the DPU and the longer latency/lower throughput appear in parentheses. MVE scalar shifts are also excluded from the scalar/vector overlap list. [Cortex-M55 r1p1 optimization guide, §3.11, Table 3-17, p. 41; §4.1, p. 46](https://support.arm.com/documentation/102692/0101)

As a scheduling implication, classify the exact assembled instructions by pipe, write stage, forwarding class, and variant before interleaving them. Then preserve dependencies and the explicit mixed-pipe ordering notes, and inspect the generated code under the intended target configuration.

## Cortex-M85 r1p0 overlap and sequence rules

For hand-scheduled MVE code on Cortex-M85 r1p0, consider the two-beat issue groups and the order of SIMD and load/store instructions together. Each 128-bit MVE instruction is processed over two 64-bit ticks. When dependencies permit, tick 1 of an older instruction can overlap tick 0 of a younger one. The issue scheduler permits that overlap only when those two beats do not require the same issue-group resource. The Cortex-M85 r1p0 optimization guide's per-instruction throughput is a peak group rate, not an application speedup. [Arm Cortex-M85 Processor Software Optimization Guide, r1p0, Issue 01 (26 April 2023), sections 2.2 and 3.1, pages 11-13; section 4.1, page 43](https://support.arm.com/documentation/107950/0100)

MVE instructions use slot 0 in tick 0 and do not occupy the issue stage in tick 1. Scalar FP shares EPU resources with MVE, so adjacent scalar FP and MVE instructions can have structural hazards. Integer/MVE structural hazards can also occur when an MVE instruction modifies scalar registers. Resource compatibility and operand dependency are separate checks: a pair with different issue groups still cannot bypass a true dependency. Do not apply the Cortex-M55 I/F/LS/SY or E2/E3 write-stage rules to M85; use the M85 tables and the exact instruction form. [Cortex-M85 r1p0 optimization guide, sections 2.2 and 3.1, pages 11-13; section 4.1, page 43](https://support.arm.com/documentation/107950/0100)

### Use representative execution groups

M85 sections 3.10-3.13 assign MVE operations to groups A, B, C, or SYS. These examples help identify pairs to inspect; they are not a complete scheduling table. [Cortex-M85 r1p0 optimization guide, sections 3.10-3.13, pages 31-40](https://support.arm.com/documentation/107950/0100)

| Representative MVE operation | Source-reported latency and throughput | Group |
| --- | --- | --- |
| Integer VADD/VSUB and many integer arithmetic operations | Latency 1; throughput 1/2 | A |
| Integer min/max reductions (VMAXV/VMAXAV/VMINV/VMINAV) | Latency 2; throughput 1/2 | A |
| Integer VMUL/VMLA and listed saturating or long multiply forms | Latency 2; throughput 1/2 | B |
| Polynomial VMULL | Latency 1; throughput 1/2 | A |
| Integer arithmetic-to-scalar reductions/accumulates (VADDLV/VADDV/VMLADAV/VMLALDAV/VMLSDAV/VMLSLDAV/VRMLALDAVH/VRMLSLDAVH) | Latency 2; throughput 1/2 | B |
| Integer VCMP/VPT | Latency 1; throughput 1/2 | C |
| Logical operations such as VAND, VEOR, VORR, and VREV | Latency 1; throughput 1 | A or B |
| FP VADD/VSUB/VCADD | Latency 2; throughput 1/2 | A |
| FP VMUL/VCMUL | Latency 3; throughput 1/2 | B |
| FP vector VCMLA/VFMA/VFMS/VFMAS | Latency 4 from multiplicands and 2 from addend; throughput 1/2 | A and B |
| VCTP, VPNOT, VPST | Latency 1; throughput 1 | SYS |

Scalar-lane transfers have their own group and direction. A general-purpose register to one vector lane, or two general-purpose registers to two 32-bit lanes, lists latency 1 and throughput 1 in group A or B. A vector lane to general-purpose register(s) lists latency 1 and throughput 1/2 in group B. The note below the lane-VMOV table refers to shift-amount and shifted-value operands despite appearing under the MOV table; do not apply that note to VMOV. Use the scalar-shift timing row for those dependencies. [Cortex-M85 r1p0 optimization guide, section 3.11, pages 37-38](https://support.arm.com/documentation/107950/0100)

For example, the Cortex-M85 r1p0 optimization guide's A, B, A, B alternation can expose overlap when operands are independent and the required resources are free; a run such as B, B, A, A is less favorable. This is an issue-group illustration, not a guaranteed cycle count. For FP and memory dependency adjustments, see [Cortex-M85 floating-point scheduling](techniques-cortex-m85-floating-point-scheduling.md) and [Cortex-M85 memory-access scheduling](techniques-cortex-m85-memory-access-scheduling.md). [Cortex-M85 r1p0 optimization guide, section 4.10, pages 46-49](https://support.arm.com/documentation/107950/0100)

The integer arithmetic-to-scalar reductions and accumulates above produce scalar values; the Cortex-M85 r1p0 optimization guide's producer/consumer adjustment makes an integer consumer see a longer delay for FP/MVE-produced scalar values because EPU execution begins two cycles later than integer/LSU execution. Keep that consumer adjustment separate from the latency-2 producer row. [Cortex-M85 r1p0 optimization guide, sections 3.10 and 4.2, pages 31-37 and 43-44](https://support.arm.com/documentation/107950/0100)

### Account for loads, stores, and loop boundaries

In the Cortex-M85 r1p0 optimization guide's examples, alternating vector loads and stores can fully overlap, but adjacent loads do not overlap each other and adjacent stores do not overlap each other. Do not extend the alternating-load/store result to every arbitrary LDST sequence. The first LDST in an LDST run overlaps the last SIMD instruction in the preceding SIMD run in the described schedules, subject to data dependencies and applicable memory conditions. [Cortex-M85 r1p0 optimization guide, section 4.10, pages 46-49](https://support.arm.com/documentation/107950/0100)

After an overlapping SIMD pair, an otherwise fully overlapping alternating LDST run exposes overlap with the next SIMD when its instruction count is even. For example, A, B, LD, ST, A permits the next A to overlap ST; A, B, LD, ST, LD, A does not permit it to overlap the final LD. After a non-overlapping SIMD pair, the corresponding alternating-run rule is odd count: A, A, LD, ST, LD, B permits B to overlap the final LD, while A, A, LD, ST, B does not. These are schematic sequences with independent operands. [Cortex-M85 r1p0 optimization guide, section 4.10, pages 46-49](https://support.arm.com/documentation/107950/0100)

If adjacent loads or adjacent stores cause a non-overlapping part of the LDST run, count the tail starting at the conflict-causing second load or store, including that instruction. The following SIMD instruction overlaps only when that tail contains an even number of instructions or exactly one instruction. The single-instruction exception is explicit: in A, A, LD, LD, B, the tail starting at the second LD has length 1, so B overlaps the final LD; in A, B, LD, LD, ST, LD, A, the tail starting at the second LD has length 3, so the final A does not overlap; and in A, A, ST, ST, LD, A, the tail starting at the second ST has length 2, so A overlaps. Preserve the stated counting rule rather than treating all LDST runs as alternating pairs. [Cortex-M85 r1p0 optimization guide, section 4.10, pages 48-49](https://support.arm.com/documentation/107950/0100)

LE/LETP has no issue cost and does not change LDST/SIMD overlap based on where it appears, but compatibility from the end of one loop body to the start of the next still matters. Unrolling can create more freedom to place independent instructions even when the loop instruction itself has no issue cost; that is a scheduling opportunity, not removal of loop-control overhead. Register pressure, true dependencies, alignment, and memory behavior remain relevant. [Cortex-M85 r1p0 optimization guide, section 4.10, pages 46-49](https://support.arm.com/documentation/107950/0100)

The Cortex-M85 r1p0 optimization guide also notes a tradeoff between scatter/gather instructions and scalar transfers to or from vector registers based on address-register pressure and bank-friendly access arrangement. Scatter/gather base-register updates do not forward to following instructions. See [Cortex-M85 memory-access scheduling](techniques-cortex-m85-memory-access-scheduling.md) for conditional address-formation options and separate store-data adjustments. [Cortex-M85 r1p0 optimization guide, section 4.10, page 49](https://support.arm.com/documentation/107950/0100)

These rules describe the Cortex-M85 r1p0 optimization guide's issue resources, dependency constraints, and example sequences. They are implementation guidance for instruction placement, not algorithm changes; inspect generated instructions and validate behavior in the intended application. [Cortex-M85 r1p0 optimization guide, sections 2.2, 3.1, and 3.10-3.15, pages 11-13 and 31-42; section 4.10, pages 46-49](https://support.arm.com/documentation/107950/0100)


## Cortex-M52 r0p3, Issue 03: beatwise MVE overlap

For Cortex-M52, use the pipeline labels and overlap rules for this core when arranging MVE work. The processor is a one-beat-per-tick machine: a 128-bit vector instruction executes as four 32-bit ticks. Up to two beatwise MVE instructions can overlap; an older instruction's ticks 2/3 can execute with a newer instruction's ticks 0/1 only when the instructions use different execution pipes. Section 3.1 also describes a one-cycle stall when issuing an MVE instruction after another, so do not infer a complete cycle schedule from the summary wording alone. The EPU runs in lock-step with the DPU, and E0 decides overlap from resource and data availability; any E0 hazard stall prevents it. [Arm China Cortex®-M52 Processor Software Optimization Guide, document 107730_0003_03_en, §§2.2 and 3.1, pp. 10–15; §4.1, p. 45](https://support.arm.com/documentation/107730/0003)

The M52 pipeline labels are SY (system registers), LS (load/store), and VF (shared vector and floating-point). Integer vector arithmetic and vector floating-point arithmetic both use VF, so alternating integer and floating-point mnemonics alone does not create different-pipe overlap. E1/E2 results forward to E0 for following instructions of the same class, but not across classes such as floating point to fixed point or vector to scalar floating point. A distinct pipe does not remove a data dependency. [Cortex-M52 r0p3 optimization guide, §2.2, pp. 13–14; §§3.10–3.15, pp. 33–44](https://support.arm.com/documentation/107730/0003)

| M52 instruction family | Representative source-reported timing and pipeline |
| --- | --- |
| Most MVE integer vector arithmetic, bitwise, compare, duplicate, move, multiply, negate, select, shift, and arithmetic-to-scalar rows | Latency 2, throughput 1/4, VF. The table notes beatwise execution for the multicycle MVE groups. |
| MVE integer lane transfers, including VMOV between a GPR and vector lane(s) | Latency 2, throughput 1, VF. |
| Listed MVE scalar shift instructions | Latency 5, throughput 1/4, DPU. |
| Most MVE floating-point arithmetic, compare, convert, multiply, and negate rows | Latency 2, throughput 1/4, VF. |
| MVE system-register transfers (VLDR/VSTR system register, VMRS, VMSR) | Latency 1, throughput 1, SY. |
| MVE VCTP, VPNOT, and VPST | Latency 2, throughput 1/4, SY. |
| Continuous and deinterleaving vector loads; continuous and interleaving vector stores | Latency 2, throughput 1/4, LS. |
| Gather loads and scatter stores | Use LS and VF. T5/T6 writeback forms execute in VF; their latency/throughput differs from the corresponding non-writeback form (see [Cortex-M52 memory-access scheduling](techniques-cortex-m-memory-access-scheduling.md#cortex-m52-r0p3-issue-03)). |

Integer reductions have element-size-dependent timing, and absolute reductions use separate rows:

| M52 reduction group | Element size | Latency | Throughput |
| --- | --- | --- | --- |
| VMAXV / VMINV | 32 / 16 / 8 bits | 5 / 9 / 13 | 1/4 / 1/8 / 1/12 |
| VMAXAV / VMINAV | 32 / 16 / 8 bits | 9 / 13 / 17 | 1/8 / 1/12 / 1/16 |
| VABAV | As listed | 9 | 1/8 |
| MVE floating-point VMAXNMV / VMINNMV and VMAXNMAV / VMINNMAV | Single precision / half precision | 5 / 9 | 1/4 / 1/8 |

The arithmetic-to-scalar integer groups VADDLV, VADDV, VMLADAV, VMLALDAV, VMLSDAV, VMLSLDAV, VRMLALDAVH, and VRMLSLDAVH report latency 2 and throughput 1/4 on VF. The M52 source does not state the M55 SRF E2/E3 write-port hazard for these rows. Do not import that M55 hazard into M52 guidance. [Cortex-M52 r0p3 optimization guide, §3.10, pp. 33–39; §§3.11–3.13, pp. 39–42](https://support.arm.com/documentation/107730/0003)

Section 4.1 lists scalar instruction classes that may overlap tick 3 of a preceding vector instruction: immediate B, BL, and CB[N]Z; low-overhead-loop instructions; Branch Future instructions (which act as NOPs); and integer arithmetic except DIV, all CSEL forms, MVE scalar shifts, and PC-modifying instructions. The Cortex-M52 r0p3 optimization guide's limits are important: an instruction that checks the stack limit does not overlap; a scalar cannot overlap a vector instruction marked with implicit LE (the last instruction in a low-overhead loop); no scalar load or store overlaps a vector instruction; and a scalar/vector dependency makes overlap unlikely. [Cortex-M52 r0p3 optimization guide, §4.1, p. 45](https://support.arm.com/documentation/107730/0003)

A useful schedule can pair an independent LS access with VF work when the relevant ticks use different pipes and E0 sees no hazard. This is a conditional scheduling opportunity, not a timing guarantee. Do not transfer M55 base-writeback, scalar-memory, or SRF hazards, or M85 pipeline-group rules, to M52. For the element-size and writeback memory rows, see [Cortex-M52 and Cortex-M55 memory-access scheduling](techniques-cortex-m-memory-access-scheduling.md).

## Cortex-M55 DSP-kernel implementation examples

The [Cortex-M55 r1p1 optimization guide](https://support.arm.com/documentation/102692/0101) describes execution rules; CMSIS-DSP v1.18.0 sources provide public FIR and biquad implementations that illustrate application kernels.

For F32 FIR scheduling, retain reusable scalar tap coefficients where useful and interleave sample loads with vector multiply/FMA work. The Q15 implementation instead loads tap vectors and uses dot-product/reduction operations. These are distinct lane mappings; preserve their accumulation and output-conversion semantics. Public implementations are available in [arm_fir_f32.c](https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/FilteringFunctions/arm_fir_f32.c) and [arm_fir_q15.c](https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/FilteringFunctions/arm_fir_q15.c); see [Helium FIR tap blocking](techniques-helium-fir-tap-blocking.md) for the register-budget and scratch-pass discussion.

For DF1 biquads, the public [F32 kernel](https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/FilteringFunctions/arm_biquad_cascade_df1_f32.c) and [F16 kernel](https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/FilteringFunctions/arm_biquad_cascade_df1_f16.c) load expanded coefficient vectors while accumulating the recurrence across four F32 or eight F16 outputs. Streaming a coefficient vector or a small scheduled set can limit coefficient live ranges, while interleaving those loads with independent FMA work is conditional on the selected core's resources and hazards; inspect the generated schedule. The coefficient transformation and numerical considerations are documented on [Helium DF1 IIR vectorization](techniques-helium-df1-iir-vectorization.md).

The independently derived four-output fourth-order MVE F32 layout has twelve four-lane coefficient vectors. Each group uses eight input positions (four prior input-history values and four current inputs) and four prior output-history values. Coefficients, the output accumulator, input scalars, pointers, loop controls, and persistent state all contribute to register demand. Keeping history in vector registers can reduce scalar general-purpose-register pressure, but uses vector registers and can require lane transfers; streaming coefficients can free vector registers at the cost of additional loads. The [ACLE MVE reference](https://arm-software.github.io/acle/mve_intrinsics/mve.html) documents the vector operations and scalar/lane transfers. Assess the complete loop, including history extraction and replacement at group boundaries, ABI saves, and possible spills. Load/FMA overlap remains conditional on core-specific scheduling rules. This example describes a design implication of the recurrence expansion; it does not establish a public CMSIS-DSP direct fourth-order implementation or a hardware performance result. See [Helium DF1 IIR vectorization](techniques-helium-df1-iir-vectorization.md) for the independent recurrence algebra.

## Reuse operands and inspect scheduling hypotheses

For a hot kernel with several independent outputs, look for values that can be loaded once and reused across more than one result. The [ML/DSP front-end optimization case study](https://github.com/Arm-Examples/Helium-Optimization/blob/bc9dfb67be64a2a5ee8e5325910a3f060a593863/Performance_analysis/ml-dsp-front-end-optimization-with-helium.md)'s CMSIS-NN matrix-multiply example reuses a right-hand-side vector across independent rows, combines useful auxiliary work with the main loop, and interleaves loads with reductions. A clear C loop can expose the same data reuse to the vectorizer; handwritten intrinsics or assembly are not the only way to express it.

When low-level scheduling is justified, interleave independent load and arithmetic groups according to the selected core's documented execution resources. Avoid assuming that one generic sequence works across Cortex-M52, Cortex-M55, and Cortex-M85. LLVM-MCA can provide a static resource and dependency hypothesis for generated code, but it does not model all dynamic alignment, memory-system, or DTCM-bank effects. Confirm the hypothesis with target-appropriate profiling and preserve each core- and revision-specific condition already documented above.

## Apply beatwise overlap only within the selected implementation

MVE exposes a fixed-width vector operation divided into 32-bit beats, while the implementation determines how many beats execute per architectural tick and which instruction overlaps are supported. The Arm Helium reference book's dual-beat example is specific to its Cortex-M55 discussion; it does not define Cortex-M52 or Cortex-M85 execution pipelines. Use the matching core and revision guidance above to determine actual resources, forwarding, and hazards. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 42–46](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

Arm's example shows a vector-wide load/MAC dependency may still allow beatwise overlap: each MAC beat depends on its corresponding loaded beat. This is an architectural possibility; check the selected core's forwarding and resource rules before scheduling. [Making Helium: Why not just add Neon? (1/4)](https://developer.arm.com/community/arm-research/b/articles/posts/making-helium-why-not-just-add-neon)

Interleaving a load with arithmetic can expose an opportunity when the instructions use compatible, separate resources, but preserve true data dependencies and exact instruction-form restrictions. A scalar operation between vector instructions is useful only when it is independent of the surrounding vector work and the selected core permits the overlap; changing mnemonic order alone does not remove a dependency. Treat the Arm Helium reference book's general load/arithmetic alternation as a scheduling hypothesis to check against the core-specific rules, not a portable schedule. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 42–44, 176–177](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

On Cortex-M55, the Arm Helium reference book calls out latency when `VIDUP` or `VDDUP` produces scatter/gather offsets. Schedule offset generation sufficiently ahead of its consuming memory instruction to respect that producer/consumer dependency, and confirm the exact instruction behavior in the applicable M55 guide. This placement advice is M55-specific and does not replace the M52 or M85 scheduling rules above. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF p. 177](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

Inspect the loop backedge as an adjacent scheduling pair. If a vector loop starts with a load such as `VLDR` and ends with compatible vector arithmetic such as `VMLA`, the final arithmetic instruction of one iteration and the next iteration's leading load may overlap when the exact core's resources, forwarding, and true dependencies permit it. Check memory bounds and preserve the implementation- and revision-specific rules; the Cortex-M85 same-resource restriction across the backedge above still applies. Different instruction classes alone do not guarantee overlap or a speedup. For cache-assisted low-overhead loop flow and recovery, see [Cortex-M branch prediction and loop control](techniques-cortex-m-branch-prediction.md). This hardware opportunity is distinct from explicit software pipelining, which moves a future iteration's load earlier and requires valid loop-entry and drain handling as described below. [Making Helium: Bringing Amdahl's law to heel (4/4)](https://developer.arm.com/community/arm-research/b/articles/posts/making-helium-bringing-amdahl-s-law-to-heel)

## Software-pipeline loads with explicit boundaries

Loading the next iteration's operands while computing the current iteration can expose memory/compute overlap when the next address and work are independent. Make the transformed loop's entry, steady-state, and drain behavior explicit: initialize the first operands before consuming them, advance state only for valid iterations, and finish the last computation without issuing a speculative preload past the valid input range. Preserve zero- and short-count behavior, the final bounds, and any vector tail or cleanup path. These checks matter because an extra preload can access outside the allocation even when the original loop did not. The Arm Helium reference book's schedule is illustrative and omits complete boundary handling, so use a verified implementation or reason from the full loop contract rather than copying its sequence. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 180–181](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

Unrolling can create room to move independent loads earlier or reuse an operand, but judge the complete callable kernel, including preload setup, loop control, drain, code size, and register saves. Keep the live vector set within available registers where practical; spills and larger entry/exit sequences can erase the benefit of a more flexible inner-loop schedule. Inspect the generated function and preserve its ABI obligations. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 157, 180–181, 210–213](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

Post-increment memory forms may combine an access with pointer advancement and remove separate scalar updates where the exact instruction form and compiler contract allow it. The updated base still creates scalar dependencies, and the overlap limits differ by implementation; retain the specific base-writeback rules in the core sections rather than transferring the Arm Helium reference book's Cortex-M55 example to other cores. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 177, 179–180](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

Branch Future instructions provide an optional architectural hint; an implementation may treat them as no-operations. The Arm Helium reference book describes them as NOPs on its Cortex-M55 implementation. Keep the fallback branch at the specified location and preserve correct behavior when the hint has no effect; do not add these instructions on the assumption that they accelerate M55 code. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 118–119](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

## Schedule two independent vectors in a Cortex-M55 DSP kernel

For independent vector groups, interleave useful loads and arithmetic so later operands are available while the current result is being completed. A two-vector block can expose this overlap in complex multiplication or in an in-place gain-and-energy pass; unrolling is useful here only when it creates independent work that the selected core and memory path can schedule. Keep the reasoning specific to the M55 implementation and inspect the generated loop rather than treating the order as a portable timing rule.

For an exact in-place gain-and-energy pass, load both current vectors before the first store when those reads are valid. Retain the second vector while the first is gained, stored, and accumulated, then process the retained vector. This can separate a store from a later read, but it does not remove a true same-address dependency. See [small-kernel fusion](../patterns/patterns-helium-small-kernel-fusion.md) for the arithmetic and [vector reductions](techniques-helium-vector-reductions.md) for the final reduction.

Make the loop entry and drain explicit. Do not preload from an empty input or past the final valid vector, and handle a count smaller than the unrolled block or a remainder that does not fill it. The public [CMSIS-DSP complex multiplication implementation](https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/ComplexMathFunctions/arm_cmplx_mult_cmplx_f32.c) is a complete reference for its own full blocks and complex tail handling. Partial assembly listings without complete operand constraints, modified-base declarations, and clobbers are not callable implementations; preserve the ABI and bounds in any replacement.

## Schedule recursive biquad work while preserving state

The register-role rotation below is a derived dataflow and allocation concept; it does not add to or replace the source-reported core-specific timing rules above. Recursive kernels expose two different kinds of work. Outputs from one recurrence still depend on earlier outputs across group boundaries; unrolling that chain does not remove the feedback dependency. Independent channel recurrences, or cascade-stage work deliberately delayed across ticks, can provide separate chains to interleave. See [DF2T lane mapping](techniques-helium-df2t-lane-mapping.md) and [DF1 recurrence vectorization](techniques-helium-df1-iir-vectorization.md) for the state and sample mappings that make those dependencies explicit.

When unrolling independent work, assign the live `d1`/`d2` values and partial results to registers according to their actual use order. A register-role rotation can keep each newly computed logical state in the register selected for the next unrolled body, avoiding copies back to fixed register names when the dataflow permits it. Keep each old state available through its final use, and establish the canonical state values for loop exits, tails, call boundaries, and stored state. Register renaming is a scheduling choice; it does not change the recurrence or make dependent outputs independent.

Budget the complete live vector set. MVE has eight Q registers, shared by state vectors, input and output vectors, coefficient vectors, partial accumulators, and shift temporaries. Budget the scalar carry word, input scalars, pointers, and loop controls separately in general-purpose registers. The DF1 four-output transform has twelve coefficient vectors, so holding every coefficient vector at once exceeds that register file. Streaming coefficients shortens their live ranges but adds loads; interleave a load with independent FMA work only when the selected core and memory path permit it. Count spills, reloads, register moves, state stores, and tail cleanup in the complete implementation.

Inspect generated assembly for spills, state-copy moves, coefficient traffic, fused-operation order, unrolled cleanup, and entry/exit handling. Compare compiler and library paths before choosing intrinsics or assembly, and keep a custom schedule only when it meets the application's numerical and performance contract on its target. Do not transfer an instruction order from a different vector ISA or core as a portable schedule. The public [CMSIS-DSP v1.18.0 DF2T kernel](https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/FilteringFunctions/arm_biquad_cascade_df2T_f32.c) is a release-specific implementation reference, not a universal register schedule.
