---
id: techniques-cortex-m-floating-point-scheduling
title: Schedule Cortex-M52 and Cortex-M55 floating-point instructions by precision and dependencies
type: techniques
architectures:
  - cortex-m52
  - cortex-m55
tags:
  - instruction-scheduling
  - floating-point
  - helium
  - mve
  - asm
confidence: source-reported
reproducibility: concept
related:
  - languages-c-float16-half-precision
  - techniques-helium-instruction-interleaving
  - techniques-cortex-m-latency-aware-scheduling
  - techniques-cortex-m-memory-access-scheduling
sources:
  - https://support.arm.com/documentation/102692/0101
  - https://support.arm.com/documentation/107730/0003
aliases:
  - Cortex-M55 floating-point scheduling
  - Cortex-M55 FP instruction timing
  - Cortex-M55 scalar and vector floating-point latency
  - Cortex-M55 VFMA versus VMLA latency
  - Cortex-M55 double precision latency
  - Cortex-M55 floating-point divide and square root
  - Cortex-M52 scalar floating-point timing
  - Cortex-M52 MVE floating-point reductions
evidence_basis: Timing values and pipeline behavior are source-reported for Cortex-M52 r0p3 and Cortex-M55 r1p1, Issue 03. Scheduling implications preserve dependencies and numerical behavior; the Cortex-M52 and Cortex-M55 optimization guides provide no measured speedups.
---

Use this page when checking scalar or MVE floating-point dependencies and operation timing on Cortex-M52 r0p3 or Cortex-M55 r1p1. Match precision, instruction form, and pipeline rules to the target core; timing tables are source-reported characteristics, not application performance measurements.

## Cortex-M55 r1p1, Issue 03

## Scalar floating-point timing by precision

In Table 3-15, the values in parentheses are the longer multiple-cycle double-precision values. Execution latency is the minimum delay for a dependent operation; throughput is the maximum instruction rate reported for the instruction group. [Cortex-M55 r1p1 optimization guide, §3.1, p. 16; §3.9, Table 3-15 and note 1, pp. 32–33](https://support.arm.com/documentation/102692/0101)

| Scalar operation | Half/single latency; throughput | Double latency; throughput |
| --- | --- | --- |
| `VADD`, `VSUB` | 2 cycles; 1 instruction/cycle | 15 cycles; 1/14 instruction/cycle |
| `VMUL`, `VNMUL` | 2 cycles; 1 instruction/cycle | 21 cycles; 1/20 instruction/cycle |
| `VFMA`, `VFNMA` | 2 cycles; 1 instruction/cycle | 24 cycles; 1/23 instruction/cycle |
| `VMLA`, `VNMLA` | 4 cycles; 1/3 instruction/cycle | 36 cycles; 1/35 instruction/cycle |

The Cortex-M55 r1p1 optimization guide describes `VMLA.F16` and `VMLA.F32` as a multiply followed serially by an add in E2, with full rounding after each operation in E3. A fused `VFMA` has a different rounding sequence. Where application accuracy, rounding, and exception behavior permit, the lower-latency fused form is a candidate to evaluate; it is not a numerically equivalent replacement in every program. Do not claim a speedup without measuring the actual workload. [Cortex-M55 r1p1 optimization guide, §2.2, p. 15; §3.9, Table 3-15, pp. 32–33](https://support.arm.com/documentation/102692/0101)

## Divide and square-root cases

| Operation and precision | Ordinary input latency; throughput | Zero, infinity, NaN, or invalid-operation row |
| --- | --- | --- |
| `VDIV` or `VSQRT`, half precision | 9 cycles; 1/8 instruction/cycle | 6 cycles; 1/5 instruction/cycle |
| `VDIV` or `VSQRT`, single precision | 16 cycles; 1/15 instruction/cycle | 6 cycles; 1/5 instruction/cycle |
| `VDIV` or `VSQRT`, double precision | 29 cycles; 1/28 instruction/cycle | 6 cycles; 1/5 instruction/cycle |

The table reports matching special-input rows for divide and square root. These are source timing cases, not a reason to change inputs or arithmetic semantics to seek a faster path. The Cortex-M55 r1p1 optimization guide says divide uses an iterative algorithm and blocks subsequent divide operations until it completes; it does not clearly state that square root blocks later divides in the same way, so do not extend that rule to square root. [Cortex-M55 r1p1 optimization guide, §3.9, Table 3-15, pp. 32–34, note 3, p. 34](https://support.arm.com/documentation/102692/0101)

## Respect EPU forwarding and MVE pipeline limits

The EPU can forward E2 and E3 results to following instructions of the same class. The Cortex-M55 r1p1 optimization guide does not support forwarding across floating-point to fixed-point classes or from vector to scalar floating-point instructions. When scheduling dependent EPU work, account for those class boundaries rather than assuming every result can feed the next operation without a stall. [Cortex-M55 r1p1 optimization guide, §2.2, pp. 14–15](https://support.arm.com/documentation/102692/0101)

Most listed MVE floating-point arithmetic, compare, convert, multiply, absolute-value, and negate operations use the F pipe and report latency 2 with throughput 1/2 instruction per cycle. MVE floating-point reductions (`VMAXNMV`, `VMAXNMAV`, `VMINNMV`, and `VMINNMAV`) instead report latency `(64 / esize) + 1` and throughput `esize / 64`; use the `esize` for the selected instruction variant. The reduction rows also point to the E2 SRF write-port hazard in §4.1.1, so include that constraint when assessing a reduction sequence. [Cortex-M55 r1p1 optimization guide, §3.12, Table 3-18 and note 1, pp. 42–43](https://support.arm.com/documentation/102692/0101)

The Cortex-M55 r1p1 optimization guide describes MVE as a two-beat-per-tick machine that can overlap up to two beatwise instructions when they use different EPU pipelines. Apply that rule with the table's pipeline assignments and dependencies: floating-point `VPT` uses the F pipe and cannot overlap with following I-pipe instructions because there is no F-to-I forwarding path. For general interleaving guidance and its limits, see [techniques-helium-instruction-interleaving](techniques-helium-instruction-interleaving.md). [Cortex-M55 r1p1 optimization guide, §3.1, p. 16; §3.12, Table 3-18, p. 42 and note 2, p. 43](https://support.arm.com/documentation/102692/0101)

These instruction timings and overlap rules help identify dependency and throughput constraints in a candidate schedule. Inspect the final instruction sequence and preserve its floating-point semantics; the [Cortex-M55 r1p1 optimization guide](https://support.arm.com/documentation/102692/0101) does not report application-level gains from rearranging these operations.


## Cortex-M52 r0p3, Issue 03

For M52 floating-point work, use the row for the actual operation and precision, and keep dependent-result latency separate from throughput. The EPU reads operands in E0, performs arithmetic in E1, and normalizes/rounds results in E2. Same-class E1/E2 results can forward to E0; forwarding is not supported across classes, including floating point to fixed point or vector to scalar floating point. Double-precision operations can recirculate partial results through E0/E1. [Arm China Cortex®-M52 Processor Software Optimization Guide, document 107730_0003_03_en, §2.2, pp. 13–14; §3.1, p. 15](https://support.arm.com/documentation/107730/0003)

| Scalar operation | Half/single latency; throughput | Double latency; throughput |
| --- | --- | --- |
| VADD, VSUB | 2; 1 | 15; 1/14 |
| VMUL, VNMUL | 2; 1 | 21; 1/20 |
| VFMA, VFNMA | 2; 1 | 24; 1/23 |
| VMLA, VNMLA | 4; 1/3 | 36; 1/35 |

The table gives the longer double-precision values in parentheses. The Cortex-M52 r0p3 optimization guide describes VMLA.F16 and VMLA.F32 as a multiply followed serially by an add in E1, with full rounding after each operation in E2. VFMA is fused and has different rounding semantics. Where the program's numerical, rounding, and exception requirements permit, the fused form is a candidate to evaluate; it is not a universally equivalent replacement and the Cortex-M52 r0p3 optimization guide reports no measured speedup. [Cortex-M52 r0p3 optimization guide, §2.2, p. 14; §3.9, pp. 31–33](https://support.arm.com/documentation/107730/0003)

| Divide or square-root case | Half precision | Single precision | Double precision |
| --- | --- | --- | --- |
| Ordinary input: latency; throughput | 11; 1/10 | 17; 1/16 | 32; 1/31 |
| Input zero, infinity, NaN, or invalid operation: latency; throughput | 5; 1/4 | 5; 1/4 | 5; 1/4 |

The ordinary rows apply to VDIV and VSQRT as printed. The Cortex-M52 r0p3 optimization guide says divides use an iterative algorithm and block subsequent divide operations until complete; it does not explicitly say that a square-root operation blocks later divides in the same way. The special-input row is timing data, not a reason to alter inputs or arithmetic semantics. [Cortex-M52 r0p3 optimization guide, §3.9, pp. 31–33](https://support.arm.com/documentation/107730/0003)

Other scalar FP absolute, compare, conversion, move, negate, and select rows report latency 2 and throughput 1. For MVE, common floating-point absolute, arithmetic, compare, convert, multiply, and negate rows use VF and report latency 2, throughput 1/4. The MVE floating-point reductions VMAXNMV/VMINNMV and VMAXNMAV/VMINNMAV report 5 and 1/4 for single precision, and 9 and 1/8 for half precision; these are beatwise multicycle operations. Half precision is not universally lower latency. [Cortex-M52 r0p3 optimization guide, §3.9, pp. 31–33; §3.12, pp. 40–42](https://support.arm.com/documentation/107730/0003)

MVE VPT and VCMP floating-point rows also use VF with latency 2 and throughput 1/4. The Cortex-M52 r0p3 optimization guide does not state the M55-specific F-to-I VPT restriction, although its general no-cross-class EPU forwarding rule still matters for dependent instructions. On M52, integer-vector and floating-point operations share VF, so simply alternating those data types does not create different-pipe overlap. For the full M52 overlap rules, see [Helium instruction interleaving](techniques-helium-instruction-interleaving.md). [Cortex-M52 r0p3 optimization guide, §2.2, pp. 13–14; §§3.12–3.13, pp. 40–42; §4.1, p. 45](https://support.arm.com/documentation/107730/0003)

These timings support dependency and precision analysis in existing code. Preserve the program's floating-point semantics, inspect the emitted operations, and measure the workload before attributing a gain to a schedule or instruction choice.