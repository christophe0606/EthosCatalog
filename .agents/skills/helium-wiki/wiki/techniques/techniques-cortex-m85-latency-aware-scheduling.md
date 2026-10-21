---
id: techniques-cortex-m85-latency-aware-scheduling
title: Schedule Cortex-M85 scalar instructions using dependencies and issue resources
type: techniques
architectures:
  - cortex-m85
tags:
  - instruction-scheduling
  - asm
  - extension-processing-unit
confidence: source-reported
reproducibility: concept
related:
  - techniques-helium-instruction-interleaving
  - techniques-cortex-m-branch-prediction
  - techniques-cortex-m85-memory-access-scheduling
  - techniques-cortex-m85-floating-point-scheduling
sources:
  - https://support.arm.com/documentation/107950/0100
aliases:
  - Cortex-M85 scalar scheduling
  - Cortex-M85 instruction latency
  - Cortex-M85 dual issue
  - M85 scalar dependency scheduling
evidence_basis: Arm's Cortex-M85 r1p0, Issue 01 pipeline description and instruction tables report minimum dependency latency, peak throughput, slot eligibility, and operand-specific timing. Scheduling advice here is an implementation implication; the Cortex-M85 r1p0 optimization guide supplies no workload speedup measurements.
---

For hand-scheduled assembly or compiler-generated hot paths on Cortex-M85 r1p0, use the final instruction encoding and the actual producer-to-consumer dependency. The timings below are source-reported for Arm Cortex-M85 Processor Software Optimization Guide, r1p0, Issue 01; they are not measured whole-loop costs and do not apply to Cortex-M55. [Arm Cortex-M85 Processor Software Optimization Guide, r1p0, Issue 01 (26 April 2023), §2.2, pp. 11–12; §3.1, p. 13](https://support.arm.com/documentation/107950/0100)

## Distinguish latency, throughput, and issue eligibility

The seven-stage integer pipeline and nine-stage scalar FP/MVE pipeline execute in order; the DPU, LSU, and EPU stall together as needed to maintain order. The IFU fetches 64 bits per cycle, enough to support decode of two instructions per cycle even when both are 32-bit instructions. M85 dual issue is therefore not limited to pairs of 16-bit instructions, but instruction dependencies and available issue resources still constrain each pair. [Cortex-M85 r1p0 optimization guide, §2.2, pp. 11–12](https://support.arm.com/documentation/107950/0100)

Execution latency is the minimum delay seen by a dependent operation. Execution throughput is the maximum instruction rate for a group across the processor microarchitecture. In the dual-issue column, `01` means slot 0 only, `00` means no dual issue, and `11` means either slot. The slot-0 resource can be claimed by an instruction from either decode slot, but only one slot-0 instruction can issue in a cycle. A throughput of 1 does not by itself mean an instruction cannot dual-issue: some rows report throughput 1 and slot field `11`. Check the exact row, encoding, operands, and dependencies. [Cortex-M85 r1p0 optimization guide, §3.1, p. 13](https://support.arm.com/documentation/107950/0100)

## Source-reported scalar timing patterns

| Instruction class or condition | Timing reported by the [Cortex-M85 r1p0 optimization guide](https://support.arm.com/documentation/107950/0100) | Scheduling interpretation |
| --- | --- | --- |
| Many listed 16-bit and 32-bit simple ALU, add/subtract, immediate, and move forms | Latency 1, throughput 2, slot field `11` | Independent eligible work may issue together; confirm the emitted forms and dependencies. |
| Note-tagged shifted-register ALU forms, `ADD`/`SUB SP plus register (T3)`, `PKHBT`/`PKHTB (T1)`, and `SXTAB`/`SXTAB16`/`SXTAH` or `UXTAB`/`UXTAB16`/`UXTAH (T1)` | Latency from the shifter source is 2, even when the immediate shift is zero | Apply the note only to the rows that cite it; do not generalize it to every ALU instruction. |
| `BFI (T1)` | Latency 2, throughput 2, slot field `11` | Separate its dependent-use latency from its peak throughput. |
| `CSEL`, `CSINC`, `CSINV`, `CSNEG (T1)` | Latency 1, throughput 1, slot field `11` | The table permits either slot even though the group's peak throughput is 1. |
| Listed packed saturating arithmetic and `USAT`/`SSAT` groups | Latency 2, throughput 1, slot field `01`; listed packed signed/unsigned add/subtract and simple extension groups commonly report 1, 1, `01` | Use the row's slot restriction and dependency latency rather than treating packed arithmetic as one uniform class. |
| `USAD8 (T1)` and `USADA8 (T1)` | `USAD8`: latency 2, throughput 1, slot `11`. `USADA8`: latency 2 from other sources and 1 from the addend, throughput 1, slot `11` | Use the shorter latency only for a dependency on the actual addend. |
| Integer multiply and multiply-accumulate groups | Listed `MUL` and other multiply groups: latency 2, throughput 1, slot `11`. Listed `MLA`/`MLS` and DSP multiply-accumulate groups: latency 2 from multiplicands and 1 from addend/minuend, throughput 1, slot `11` | Keep the addend-forwarding case specific to the operand that feeds it. |
| `SDIV` and `UDIV (T1)` | `SDIV`: latency 6–21, throughput 1/19–1/4. `UDIV`: latency 5–20, throughput 1/18–1/3. The dual-issue field is a dash. | Division latency depends on operands, and iterative division blocks a following divide until completion; do not assume a fixed cost or assign a slot property. |
| Listed MVE integer scalar shifts | Immediate shifts: latency 1, throughput 1, slot `11`. Register shifts: latency 2 from the shift amount and 1 from the shifted value, throughput 1, slot `11` | Identify which source operand is on the critical dependency. |

These entries summarize the arithmetic/logical, move/shift, multiply/divide, and MVE scalar-shift tables. The divide latency is highest when the divisor is much smaller than the dividend and lowest when their magnitudes are similar or for divide by zero. The divide-by-zero timing is a table case, not a reason to change program semantics. [Cortex-M85 r1p0 optimization guide, §3.3, pp. 15–21; §3.4, p. 22; §3.5, pp. 22–24; §3.11, p. 38](https://support.arm.com/documentation/107950/0100)

The miscellaneous table lists `CLRM (T1)` at latency 4, throughput 1/4, slot `00`. It also prints throughput 2 for `NOP (T2)` and throughput 3 for `NOP (T1)`, both slot `11`; these NOP rows do not imply equivalent arithmetic execution capacity. The Cortex-M85 r1p0 optimization guide says `PLI` acts as a NOP. Do not add NOPs or clear extra registers on the basis of these table entries. [Cortex-M85 r1p0 optimization guide, §3.8, p. 28](https://support.arm.com/documentation/107950/0100)

## Account for producer and consumer classes

Scalar FP and MVE execution starts in E3, two cycles after integer and load/store execution starts. As a result, FP/MVE consumers can see integer or load results two cycles sooner than the producer's table latency alone suggests; integer consumers see correspondingly longer latency from FP/MVE instructions that produce scalar-register values. Use both producer and consumer classes when scheduling mixed code. For store-data forwarding cases, see [Cortex-M85 memory-access scheduling](techniques-cortex-m85-memory-access-scheduling.md); for detailed FP timing, see [Cortex-M85 floating-point scheduling](techniques-cortex-m85-floating-point-scheduling.md). [Cortex-M85 r1p0 optimization guide, §4.2, pp. 43–44](https://support.arm.com/documentation/107950/0100)

## Scheduling implications

Inspect the hot-path assembly, preserve true dependencies and program behavior, and consider placing already-available independent work between a producer and its dependent consumer. For dual issue, check both instructions' slot eligibility and the resource used; for divide, avoid building a schedule around one assumed latency. These are practical implications of the [Cortex-M85 r1p0 optimization guide](https://support.arm.com/documentation/107950/0100) timing tables, not measured speedups. Confirm any expected gain on the target workload. [Cortex-M85 r1p0 optimization guide, §2.2, pp. 11–12; §3.1, p. 13; §§3.3–3.5, pp. 15–24](https://support.arm.com/documentation/107950/0100)
