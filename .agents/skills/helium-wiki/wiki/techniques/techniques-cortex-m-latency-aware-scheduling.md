---
id: techniques-cortex-m-latency-aware-scheduling
title: Schedule Cortex-M52 and Cortex-M55 scalar instructions by latency and dependencies
type: techniques
architectures:
  - cortex-m52
  - cortex-m55
tags:
  - instruction-scheduling
  - asm
confidence: source-reported
reproducibility: concept
related:
  - techniques-cortex-m-thumb-dual-issue
  - techniques-helium-instruction-interleaving
  - techniques-cortex-m-memory-access-scheduling
  - techniques-cortex-m-floating-point-scheduling
sources:
  - https://support.arm.com/documentation/102692/0101
  - https://support.arm.com/documentation/107730/0003
aliases:
  - Cortex-M55 scalar instruction scheduling
  - Cortex-M55 instruction latency
  - scalar Thumb scheduling
  - Cortex-M52 scalar instruction scheduling
  - Cortex-M52 instruction latency and dependency costs
evidence_basis: Cortex-M52 r0p3 and Cortex-M55 r1p1 pipeline descriptions and instruction timing tables are source-reported; scheduling choices are implications rather than measured speedups.
---

Use this page when reviewing hand-scheduled Thumb code or hot-path compiler output on Cortex-M52 r0p3 or Cortex-M55 r1p1. Match the core, encoding, operand conditions, and dependencies to its own timing tables; the sections below keep each core's rules separate. These figures are source-reported instruction characteristics, not application measurements.

## Cortex-M55 r1p1, Issue 03

## Read the timing values as separate measures

The Cortex-M55 r1p1 optimization guide defines execution latency as the minimum delay observed by an operation that depends on an instruction group. Execution throughput is the maximum instruction rate for that group across the processor microarchitecture. A low latency does not imply the same throughput, and neither value by itself gives the cycle count of a loop. T1/T2 and other encoding variants have separate rows; use the row for the encoding and operand conditions present in the final code. [Cortex-M55 r1p1 optimization guide, §3.1, p. 16](https://support.arm.com/documentation/102692/0101)

The integer pipeline has four stages. Cortex-M55 is fully in order: a stall in decode or execution prevents later instructions from progressing. Scalar operands for most arithmetic are read in decode; simple arithmetic commonly completes in EX, while SIMD, saturating, multiply, and other complex operations use CX. Arithmetic results can be forwarded from EX and CX to decode. These facts make dependency latency relevant to ordering, but do not guarantee that arbitrary independent instructions will overlap or remove a stall. [Cortex-M55 r1p1 optimization guide, §2.2, pp. 11–15](https://support.arm.com/documentation/102692/0101)

## Source-reported scalar timing patterns

| Instruction class or condition | Timing reported by the [Cortex-M55 r1p1 optimization guide](https://support.arm.com/documentation/102692/0101) | Scheduling use |
| --- | --- | --- |
| Many 32-bit Thumb basic arithmetic and logical forms | Commonly latency 1, throughput 1 instruction/cycle. Some rows show conditional parenthetical values. | Check the specific table row and note; do not treat every ALU encoding as identical. |
| 32-bit scalar SIMD, saturating, packed-halfword/SEL, and several extend-and-add forms | The listed groups commonly have latency 2, throughput 1. | Include the extra dependent-operation delay when comparing a chain against an equivalent sequence. |
| Note-tagged shifted-register ALU forms | The notes say a nonzero shift raises latency from 1 to 2 and gives throughput 1. Some table rows print additional parenthetical throughput alternatives. | Apply the condition to the listed encoding only. In particular, the ORN/ORR register row prints throughput `1(1/2)` while Note 2 describes nonzero shifts as throughput 1; the [Cortex-M55 r1p1 optimization guide](https://support.arm.com/documentation/102692/0101) does not make the `1/2` condition clear, so verify that exact case rather than assigning it by inference. |
| Selected 32-bit ADD/SUB forms that write SP, and listed 16-bit ADD SP forms | Reusing EX for the stack-limit check can add a bubble; the affected rows report latency 2 and throughput 1/2. | Account for the row-specific cost. The [Cortex-M55 r1p1 optimization guide](https://support.arm.com/documentation/102692/0101) also lists 32-bit immediate ADDW/SUBW SP forms at latency 1, throughput 1, so do not generalize the penalty to every SP update. Preserve the stack-limit check. |
| 16-bit Thumb instructions | Dual issue is limited to eligible encodings. Some immediate ADD and MOV-immediate forms reach throughput 2; many other 16-bit groups remain at throughput 1. | Narrow encoding alone does not mean dual issue; check the row's slot eligibility and operands. |
| 32-bit MUL and listed scalar multiply/accumulate groups; 16-bit MUL | Latency 2, throughput 1. The 16-bit MUL is dual-issue slot 0 only. | Leave the dependent-use distance in the critical path; observe the slot restriction when considering a 16-bit pair. |
| SDIV and UDIV | Operand-dependent latency from 2 to 12 cycles; an iterative divide blocks subsequent divide instructions until it finishes. | Assess actual numerator and denominator values on the application's critical path; do not assume a fixed divide cost. |

These values and conditions are summarized from the arithmetic/logical and move tables and their notes. In particular, the Cortex-M55 r1p1 optimization guide reports many basic 32-bit arithmetic/logical operations at 1/1, but has explicit exceptions and conditional entries; selected 16-bit ADD SP forms create a stack-limit-check bubble, while wide immediate SP forms have their own 1/1 rows. [Cortex-M55 r1p1 optimization guide, §3.3, pp. 18–24; §3.4, pp. 24–25; §3.5, pp. 25–27](https://support.arm.com/documentation/102692/0101)

For SDIV/UDIV, the Cortex-M55 r1p1 optimization guide defines `DIFF_SIGN` as the denominator's leading-sign-bit count minus the numerator's; for UDIV the counts are leading zeros. If `DIFF_SIGN < 0`, it reports latency 3 and throughput 1/2. If `DIFF_SIGN >= 0`, latency is `4 + ceil(DIFF_SIGN / 4)` and throughput is `1 / (3 + ceil(DIFF_SIGN / 4))`. Divide-by-zero is listed as latency 2 and throughput 1; that timing entry is a reference case, not a recommendation to divide by zero or a guarantee about exception configuration. Preserve the program's arithmetic and fault behavior when evaluating alternatives. [Cortex-M55 r1p1 optimization guide, §3.5, pp. 25–27](https://support.arm.com/documentation/102692/0101)

## Scheduling implications

When independent work is already available, consider placing it before the consumer of a longer-latency result, while preserving true dependencies and program behavior. Compare equivalent instruction sequences using their actual encodings, operand shifts, SP effects, multiply/divide behavior, and dual-issue eligibility. These are inferences from the Cortex-M55 r1p1 optimization guide's latency and pipeline descriptions; the Cortex-M55 r1p1 optimization guide does not report measured gains for such rearrangements. [Cortex-M55 r1p1 optimization guide, §2.2, pp. 11–15; §3.1, p. 16; §§3.3–3.5, pp. 18–27](https://support.arm.com/documentation/102692/0101)

Some other scalar timing rows are useful only for particular existing work: 16-bit MOV-immediate has throughput 2, while common 32-bit MOV/MOVT forms have throughput 1; MOV PC,Rm is single-issue and has a separate parenthetical timing. CLRM clears two registers per cycle; for `num_regs` registers, the Cortex-M55 r1p1 optimization guide gives `N = floor((num_regs + 1) / 2)`, latency `N + 1`, and throughput `1/N`. Consider CLRM when the program already needs to clear several scalar registers, not as a reason to clear extra state. [Cortex-M55 r1p1 optimization guide, §3.4, pp. 24–25; §3.8, p. 31](https://support.arm.com/documentation/102692/0101)

The same miscellaneous table says PLI acts as a NOP, so do not infer a prefetch benefit from it. PLD/PLDW rows report latency 1 and throughput 1, but the Cortex-M55 r1p1 optimization guide does not establish a useful prefetch distance or application speedup. 16-bit NOP and IT rows can reach throughput 2 and dual issue in either slot; adding NOPs is not a general optimization. [Cortex-M55 r1p1 optimization guide, §3.8, p. 31](https://support.arm.com/documentation/102692/0101)

## Limits

The table values describe instruction timing with required resources and data available; they are not end-to-end measurements. The pipeline overview says unaligned accesses are decomposed into aligned requests and stall the original instruction in EX, and the load/store unit stalls the DPU until read data is available. Check the complete hot path and target binary when memory or instruction-fetch behavior can affect the result. No measured speedups are supplied by the Cortex-M55 r1p1 optimization guide. [Cortex-M55 r1p1 optimization guide, §2.2, pp. 12–15; §§3.1, 3.3–3.8, pp. 16–31](https://support.arm.com/documentation/102692/0101)


## Cortex-M52 r0p3, Issue 03

Cortex-M52 scalar code should be reviewed against the actual Thumb encoding, operand shift, destination register, and producer-consumer dependency. The processor is fully in order: a decode or execution stall prevents later instructions from progressing. Arithmetic operands are read in decode, and EX/CX results can forward to decode; latency and throughput remain separate source-reported measures, not application cycle counts. [Arm China Cortex®-M52 Processor Software Optimization Guide, document 107730_0003_03_en, §2.2, pp. 10–14; §3.1, p. 15](https://support.arm.com/documentation/107730/0003)

| Instruction group or condition | M52 timing reported by the [Cortex-M52 r0p3 optimization guide](https://support.arm.com/documentation/107730/0003) | Scheduling implication |
| --- | --- | --- |
| Many 32-bit Thumb basic ALU, arithmetic, logical, bit-field, saturating, packed, and extend-add rows | Commonly latency 1, throughput 1. Exceptions include `USAD8`/`USADA8` and `TT`, `TTT`, `TTA`, `TTAT`, reported as latency 2, throughput 1. | Use the row for the operation and encoding in the final binary; do not assign one timing to every SIMD or saturating instruction. |
| Note 1 tagged `ADD`/`SUB` register and SP-plus-register forms | Non-`LSL`, or `LSL` by more than 4: latency 2, throughput 1. Otherwise 1 and 1. If the result writes SP, the stack-limit check recycles it in EX: latency 2, throughput 1/2. | Preserve the SP update and stack check. A shifted operand can change the row's timing. |
| Note 2 tagged logical shifted-register forms | Non-`LSL`, or `LSL` by more than 4: latency 2, throughput 1. Otherwise 1 and 1. | The M52 threshold is a shift amount greater than 4; use this M52 note rather than another core's condition. |
| `ORN`/`ORR` register row | The table prints latency `1(2)` and throughput `1(1/2)` with Note 2. The note describes the shift case as throughput 1, but does not clearly assign the table's parenthesized throughput alternative. | Preserve the printed ambiguity; do not invent a condition for the `1/2` value. `ORR` also appears in another table row. |
| Narrow `ADD SP` forms; wide immediate SP forms | Selected narrow SP additions report latency 2, throughput 1/2 because of the stack-limit-check bubble. Wide immediate `ADD SP`, `ADDW SP`, `SUB SP`, and `SUBW SP` rows report 1 and 1. | Do not generalize the narrow SP penalty to all SP updates. |
| `MOV` forms | Wide immediate/register/`MOVT` forms report 1 and 1. Narrow `MOV` immediate reports 1 and 2. The separate narrow `MOV (T1)` row prints latency `1(4)`, throughput `2(1/4)`, dual-issue `11(00)`; `MOV PC, Rm` is single issue. | Check the exact row and PC operand before using a narrow form as an issue opportunity. |
| `MUL` and listed scalar multiply-accumulate groups | Latency 2, throughput 1. Narrow `MUL (T1)` is slot 0 only. | Keep dependent-use delay and slot eligibility in the schedule. The [Cortex-M52 r0p3 optimization guide](https://support.arm.com/documentation/107730/0003) does not state the M85 addend-forwarding timing; do not transfer it. |
| `SDIV`/`UDIV` | Data-dependent latency 2–20 and throughput 1/19–1. The iterative divider blocks later divide operations until completion. For nonzero division, let `DIFF_SIGN = Count_leading_sign_bit(denominator) - Count_leading_sign_bit(numerator)`; for `UDIV`, the counts are leading zeros. If `DIFF_SIGN < 0`, latency is 3 and throughput 1/2. Otherwise latency is `4 + ceil(DIFF_SIGN / 2)` and throughput is `1 / (3 + ceil(DIFF_SIGN / 2))`. Divide by zero is listed as 2 and 1. | Treat divide-by-zero as a timing-table reference case, not an input recommendation or a guarantee about exception configuration. The divisor in this M52 formula is 2. |
| `CLRM` | Clears one register per cycle; for `N = num_regs`, latency is `N + 1` and throughput is `1/N`. | Apply this only when the program already needs to clear those registers; it is not a reason to clear extra state. |
| Security instructions in existing code | `PAC`, `PACBTI`, `PACG`, `AUT`, and `AUTG` report 2 and 1; `BXAUT` reports 4 and 1/4; `BTI` reports 1 and 1. | Account for required authentication and landing-pad work while preserving its security semantics. Do not remove these instructions for timing. |

The Cortex-M52 r0p3 optimization guide also reports the listed MVE scalar shift instructions (`ASRL`, `LSLL`, `LSRL`, `SQRSHR`, `SQRSHRL`, `SQSHL`, `SQSHLL`, `SRSHR`, `SRSHRL`, `UQRSHL`, `UQRSHLL`, `UQSHL`, `UQSHLL`, `URSHR`, and `URSHRL`) on the DPU at latency 5 and throughput 1/4. It gives no CPACR-dependent alternative or per-operand shift distinction for these rows. [Cortex-M52 r0p3 optimization guide, §3.11, p. 40](https://support.arm.com/documentation/107730/0003)

These timing rows are a basis for checking dependencies and candidate schedules, not measured speedups. Preserve flags, PC/SP effects, arithmetic, exception, and security behavior; inspect the emitted code and measure the real workload before claiming a gain. [Cortex-M52 r0p3 optimization guide, §§2.2–3.5, pp. 10–26; §3.8, p. 30; §3.11, p. 40](https://support.arm.com/documentation/107730/0003)