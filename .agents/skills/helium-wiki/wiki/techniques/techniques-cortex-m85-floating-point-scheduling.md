---
id: techniques-cortex-m85-floating-point-scheduling
title: Schedule Cortex-M85 floating-point work by precision, dependency and resource use
type: techniques
architectures:
  - cortex-m85
tags:
  - floating-point
  - instruction-scheduling
  - helium
  - mve
  - asm
  - half-precision
  - extension-processing-unit
confidence: source-reported
reproducibility: concept
related:
  - techniques-helium-instruction-interleaving
  - techniques-cortex-m85-memory-access-scheduling
  - techniques-cortex-m85-latency-aware-scheduling
  - languages-c-float16-half-precision
sources:
  - https://support.arm.com/documentation/107950/0100
aliases:
  - Cortex-M85 FP scheduling
  - M85 floating point latency
  - M85 scalar and vector FP timing
  - Cortex-M85 VFMA scheduling
  - M85 fused multiply accumulate
evidence_basis: Arm Cortex-M85 r1p0, Issue 01 scalar FP and MVE instruction tables and dependency guidance. Timings are source-reported and precision/encoding specific; no workload speedups are measured.
---

For a Cortex-M85 r1p0 floating-point hot path, schedule from the actual precision, instruction encoding, producer operand, and scalar or vector execution resources. The Cortex-M85 r1p0 optimization guide's latency is a minimum dependency delay and throughput is a peak group rate; neither is a measured application cycle count. Choosing a lower precision or replacing a fused operation is valid only when the required numerical behavior is preserved. [Arm Cortex-M85 Processor Software Optimization Guide, r1p0, Issue 01 (26 April 2023), sections 2.2 and 3.1, pages 12-13](https://support.arm.com/documentation/107950/0100)

## Scalar FP timing depends on precision and operand

The EPU is single issue and accepts one scalar FP instruction per cycle for half, single, or double precision when resources allow. Scalar FP shares resources with MVE, so adjacent scalar FP and MVE instructions can have structural conflicts. The table below summarizes selected scalar arithmetic rows; the Cortex-M85 r1p0 optimization guide does not assign the double-precision ranges to operand conditions, so retain them as ranges rather than inventing such conditions. [Cortex-M85 r1p0 optimization guide, section 2.2, page 12; section 3.1, page 13; section 3.9, pages 29-31; section 4.1, page 43](https://support.arm.com/documentation/107950/0100)

| Scalar operation | Half/single precision | Double precision | Notes |
| --- | --- | --- | --- |
| `VADD`/`VSUB` T1 | Latency 2; throughput 1; field `01` | Latency 3-8; throughput 1/2 to 1/7; field `01` | Double range has no operand condition in the [Cortex-M85 r1p0 optimization guide](https://support.arm.com/documentation/107950/0100)'s notes. |
| `VMUL` T1 / `VNMUL` T2 | Latency 3; throughput 1; field `01` | Latency 5-10; throughput 1/3 to 1/8; field `01` | Keep precision-specific ranges. |
| Fused `VFMA`/`VFMS`/`VFNMA`/`VFNMS` T1 | Latency 4 from multiplicands and 2 from addend; throughput 1; field `01` | Latency 10-15; throughput 1/7 to 1/12; field `01` | Multiplicand and addend dependencies differ. |
| Nonfused `VMLA`/`VMLS`/`VNMLA`/`VNMLS` T1 | Latency 5 from multiplicands and 2 from addend; throughput 1; field `01` | Latency 11-16; throughput 1/7 to 1/12; field `01` | Do not treat these as numerically interchangeable with fused forms. |
| `VDIV` T1 | Half: latency 6, throughput 1/5, field `11`; single: latency 18, throughput 1/17, field `11` | Latency 32; throughput 1/31; field `01` | Iterative divide blocks a following divide or square root until completion. |
| `VSQRT` T1 | Half: latency 6, throughput 1/5, field `11`; single: latency 17, throughput 1/16, field `11` | Latency 31; throughput 1/30; field `01` | Iterative square root blocks a following divide or square root until completion. |

For divide and square root when the input is zero, infinite, or NaN, or the operation is invalid, the Cortex-M85 r1p0 optimization guide lists latency 6 and throughput 1/5 for all precisions, with field `11(01)`. This is a specific table case; do not use it as a general timing assumption. [Cortex-M85 r1p0 optimization guide, section 3.9, pages 29-31](https://support.arm.com/documentation/107950/0100)

Other scalar rows list `VABS`, `VMAXNM`, `VCMP`, `VNEG`, and `VSEL` generally at latency 1 and throughput 1, with field `11(01)`, and scalar conversions/rounding generally at latency 2 and throughput 1 with field `11(01)`. The parenthetical issue-field conditions are not clearly defined in the Cortex-M85 r1p0 optimization guide notes; do not assign inferred precision conditions. [Cortex-M85 r1p0 optimization guide, section 3.9, pages 29-31](https://support.arm.com/documentation/107950/0100)

Scalar `VMOV`/`VINS`/`VMOVX` transfer rows generally report latency 1 and throughput 1 with field `11(01)`. The Cortex-M85 r1p0 optimization guide's note for a `VMOV` between two general-purpose registers and two S registers says latency is 4 when S registers are written and the two S-register sources are not in one D register. Keep that source condition as written; do not simplify it into a general penalty for crossing D registers. [Cortex-M85 r1p0 optimization guide, section 3.9, pages 29-31](https://support.arm.com/documentation/107950/0100)

## MVE FP uses vector groups and distinct dependency timings

The MVE FP rows use group A for listed add/subtract work and group B for multiply work, while comparisons use group C. These values are representative of the specified operations and are not scalar FP timings. [Cortex-M85 r1p0 optimization guide, section 3.12, pages 38-40](https://support.arm.com/documentation/107950/0100)

| MVE FP operation | Latency | Throughput | Group |
| --- | ---: | ---: | --- |
| `VABS` | 1 | 1/2 | A |
| `VABD` | 2 | 1/2 | A |
| `VADD`/`VSUB`/`VCADD` | 2 | 1/2 | A |
| `VMUL`/`VCMUL` | 3 | 1/2 | B |
| Vector min/max | 1 | 1/2 | B |
| Scalar min/max reductions | 2 | 1/2 | B |
| Conversion/rounding | 2 | 1/2 | B |
| Negation | 1 | 1/2 | B |
| Compare/`VPT` | 1 | 1/2 | C |
| `VCMLA`/`VFMA`/`VFMS`/`VFMAS` | 4 from multiplicands; 2 from addend | 1/2 | A and B |

The Cortex-M85 r1p0 optimization guide's footnote for vector multiply-accumulate says these instructions appear to use B to older adjacent instructions and A to earlier adjacent instructions. Its older/earlier wording is ambiguous, so it does not support a firm before/after ordering recipe. Treat multiply-accumulate as a mixed-resource instruction and verify the exact schedule. [Cortex-M85 r1p0 optimization guide, section 3.12, pages 38-40](https://support.arm.com/documentation/107950/0100)

## Account for cross-pipeline dependencies and status writes

EPU execution starts two cycles later than integer and LSU execution. Consequently, FP/MVE consumers can observe shorter delays from integer/load producers than the producer table latency alone suggests, while integer consumers of FP/MVE-produced scalar register values see longer delays. This is a consumer-specific adjustment, not a rewritten latency table. For store-data timing, distinguish ordinary vector stores from `VST2`/`VST4`; see [Cortex-M85 memory-access scheduling](techniques-cortex-m85-memory-access-scheduling.md). [Cortex-M85 r1p0 optimization guide, section 4.2, pages 43-44](https://support.arm.com/documentation/107950/0100)

For MVE system operations, `VCTP`/`VPNOT`/`VPST` list latency 1 and throughput 1 in SYS. `VMRS` lists latency 1 and throughput 1, with no dual-issue value given. `VMSR` ordinarily lists latency 1 and throughput 1; a write to FPSCR instead has parenthetical latency 4 and throughput 1/4, with no dual-issue value given. Do not infer a slot assignment, and preserve required status and rounding behavior when moving such writes. [Cortex-M85 r1p0 optimization guide, section 3.13, page 40](https://support.arm.com/documentation/107950/0100)

## Consider splitting vector multiply-accumulate only after checking semantics

The Cortex-M85 r1p0 optimization guide reports an extra structural EPU issue-hazard cycle between the multiply and add portions of vector FP multiply-accumulate operations (including `VCMLA`, `VMLA`, `VFMA`, and `VFMS`). It says that in some schedules, separate vector multiply and add can improve placement or overlap, especially when vector loads or stores can be placed between them. The benefit is conditional and may require different registers; it is not a blanket rewrite rule. [Cortex-M85 r1p0 optimization guide, section 4.10, page 49](https://support.arm.com/documentation/107950/0100)

Splitting a fused operation adds a separate intermediate rounding operation. Check the required numerical behavior before making this change, then measure the emitted schedule on the target. An issue-placement opportunity does not establish equivalent results or a workload speedup. [Cortex-M85 r1p0 optimization guide, section 4.10, page 49](https://support.arm.com/documentation/107950/0100)

All scalar and vector timing values here are scoped to the Cortex-M85 r1p0, Issue 01 guide. Use exact precision, encoding, dependency, and issue-resource rows; do not import Cortex-M55 forwarding rules or invent conditions for ambiguous footnotes. [Cortex-M85 r1p0 optimization guide, section 2.2, page 12; section 3.1, page 13; section 3.9, pages 29-31; sections 3.12-3.13, pages 38-40; sections 4.1-4.2 and 4.10, pages 43-44 and 49](https://support.arm.com/documentation/107950/0100)
