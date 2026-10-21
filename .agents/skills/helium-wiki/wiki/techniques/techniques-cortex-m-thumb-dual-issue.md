---
id: techniques-cortex-m-thumb-dual-issue
title: Use eligible 16-bit Thumb encodings for Cortex-M52 and Cortex-M55 dual issue
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
  - techniques-cortex-m-latency-aware-scheduling
  - techniques-cortex-m-branch-prediction
sources:
  - https://support.arm.com/documentation/102692/0101
  - https://support.arm.com/documentation/107730/0003
aliases:
  - Cortex-M55 Thumb dual issue
  - Cortex-M55 16-bit instruction pairing
  - Cortex-M55 Thumb instruction slots
  - Cortex-M52 Thumb dual issue
  - Cortex-M52 16-bit instruction slots
evidence_basis: Cortex-M52 r0p3 and Cortex-M55 r1p1, Issue 03 instruction tables report eligible encodings for limited dual issue. Recommendations to inspect encodings are implementation implications; no speedup is measured.
---

Use this page when reviewing a hot path where 16-bit Thumb instruction pairing may affect execution on Cortex-M52 r0p3 or Cortex-M55 r1p1. Each core has its own eligibility and timing rows; use the matching section and verify the emitted instructions, dependencies, and operands. No speedup is measured by either guide.

## Cortex-M55 r1p1, Issue 03

## Read the slot and timing fields together

The Cortex-M55 r1p1 optimization guide defines execution latency as the minimum delay for an operation that depends on an instruction group, and execution throughput as the maximum instruction rate for that group across the processor microarchitecture. They describe different limits. In the dual-issue column, `01` means the instruction is eligible from slot 0, `11` means it is eligible from either slot, and `00` means it is not dual-issuable. A slot value does not override the row's latency, throughput, notes, dependencies, or applicable instruction-pair rules. [Cortex-M55 r1p1 optimization guide, §3.1, p. 16](https://support.arm.com/documentation/102692/0101)

Representative 16-bit instruction groups in the tables include:

| Table eligibility | Representative instructions | Qualification |
| --- | --- | --- |
| `11` (either slot) | Immediate ADD and SUB forms, immediate CMP, immediate MOV, sign- and zero-extension (`SXTB`, `SXTH`, `UXTB`, `UXTH`), `NOP`, and `IT` | The applicable row reports throughput 2 for these groups. The SP-based ADD rows are exceptions: some have latency 2 and throughput 1/2 because an SP result is recycled for the stack-limit check. |
| `01` (slot 0 only) | Register ALU operations such as `AND`, `BIC`, `CMP`, `EOR`, and `ORR`; register MOV/MVN forms; `MUL`; single loads and stores | These rows generally report throughput 1. `ADD (register) (T2)` is marked `01(00)`: its note says it does not dual-issue when `Rd=PC` or `Rm=PC`. |
| `00` (no dual issue) | `CMN (register) (T1)`, `CBZ`/`CBNZ`, load/store-multiple operations, `PUSH`, and `POP` | The corresponding rows mark these instructions as not dual-issuable; multiple-register rows also report their own `N`-dependent throughput. |

These are examples, not a complete pairing recipe. Use the exact table row for the emitted encoding, operands, and instruction effects. In particular, do not infer eligibility for every 16-bit instruction from its width alone. [Cortex-M55 r1p1 optimization guide, §3.2, Tables 3-1–3-2, pp. 17–18; §3.3, Table 3-4, pp. 23–24; §3.4, Table 3-6, p. 25; §3.5, Table 3-8, p. 27; §§3.6–3.8, Tables 3-10–3-14, pp. 29–31](https://support.arm.com/documentation/102692/0101)

## Preserve row-specific restrictions

- **SP updates:** The 16-bit `ADD SP` rows with the stack-limit-check note incur a pipeline bubble, with latency 2 and throughput 1/2. Do not replace an SP update with another form on the assumption that every narrow ADD is throughput 2; use the exact row and preserve stack behavior. [Cortex-M55 r1p1 optimization guide, §3.3, Table 3-4 and note 2, p. 24](https://support.arm.com/documentation/102692/0101)
- **PC operands:** The register ADD and MOV rows carry PC-specific restrictions. The Cortex-M55 r1p1 optimization guide says `ADD (register) (T2)` cannot dual-issue when `Rd` or `Rm` is PC; it separately says `MOV PC, Rm` can only be single-issued. [Cortex-M55 r1p1 optimization guide, §3.3, Table 3-4 and note 1, p. 24; §3.4, Table 3-6 and note 1, p. 25](https://support.arm.com/documentation/102692/0101)
- **Branches:** `CBZ` and `CBNZ` are marked `00`, while 16-bit immediate `B (T2)` is marked `11`; the table also reports branch latency behavior for backward branches. Note 3 on p. 18 is internally unclear: it describes a conditional branch as the first instruction in a pair, then says the first instruction must be an unconditional immediate `B (T2)`. Treat that wording as unresolved and do not derive a branch-pair recipe from it. [Cortex-M55 r1p1 optimization guide, §3.2, Table 3-2, pp. 17–18](https://support.arm.com/documentation/102692/0101)
- **Dependencies and scheduling:** The decode stage reads operands for most instructions and contains hazard logic; the Cortex-M55 r1p1 optimization guide also describes forwarding into decode. Thus table eligibility alone does not establish that a particular dependent pair can issue together. Check the actual instruction sequence and applicable architectural pairing rules. [Cortex-M55 r1p1 optimization guide, §2.2, p. 13; §3.1, p. 16](https://support.arm.com/documentation/102692/0101)
- **Multiple-register transfers:** The listed 16-bit LDM/POP and store-multiple rows are not dual-issuable. The Cortex-M55 r1p1 optimization guide gives `N = floor((num_regs + 1) / 2)` and throughput `1/N` for these groups. [Cortex-M55 r1p1 optimization guide, §3.6, Table 3-10, p. 29; §3.7, Table 3-12, p. 30](https://support.arm.com/documentation/102692/0101)
- **NOP and IT:** Their 16-bit table rows are marked `11` with throughput 2, but this does not make inserting NOPs a useful general optimization. [Cortex-M55 r1p1 optimization guide, §3.8, Table 3-14, p. 31](https://support.arm.com/documentation/102692/0101)

For implementation work, inspect the assembled hot path to confirm which Thumb widths and operands the compiler emitted, then check whether the eligible instructions actually form legal pairs. Any expected throughput benefit is an inference from the Cortex-M55 r1p1 optimization guide's tables and must be measured on the target workload; the Cortex-M55 r1p1 optimization guide supplies no measured speedup for this instruction-selection strategy. The timing tables describe instruction behavior and do not by themselves establish end-to-end performance. [Cortex-M55 r1p1 optimization guide, §2.2, pp. 12–13; §3.1, p. 16](https://support.arm.com/documentation/102692/0101)


## Cortex-M52 r0p3, Issue 03

The M52 supports limited dual issue for eligible 16-bit Thumb instructions. The IFU fetches 32 bits at a time and the fetch-data stage can issue one 32-bit instruction or up to two 16-bit instructions; decode has two register-read ports for single issue and a third for arithmetic dual-issue cases. Hazards and forwarding still constrain a pair. The Cortex-M52 r0p3 optimization guide defines latency as the minimum delay seen by a dependent operation and throughput as a maximum instruction rate, so neither alone proves that a particular pair issues together. [Arm China Cortex®-M52 Processor Software Optimization Guide, document 107730_0003_03_en, §2.2, pp. 11–12; §3.1, p. 15](https://support.arm.com/documentation/107730/0003)

In the table, 01 means slot 0 only, 11 means either slot, and 00 means not dual-issuable. Parenthesized table values are retained as printed.

| 16-bit group or form | M52 latency / throughput / issue field | Qualification |
| --- | --- | --- |
| Immediate ADD, SUB, and CMP forms | Common rows report latency 1, throughput 2, field 11. | The narrow SP rows are exceptions: selected ADD SP forms report latency 2 and throughput 1/2 because the result is recycled for the stack-limit check, even where the table shows 11. |
| Register ALU forms such as ADC, ADD, AND, BIC, CMP, EOR, ORR, REV, REV16, REVSH, and TST | Common rows report latency 1, throughput 1, field 01; CMN (register) (T1) is 00. | For ADD (register) (T2), the field is printed 01(00); the note says it does not dual-issue when Rd=PC or Rm=PC. |
| SXTB, SXTH, UXTB, UXTH (T1) | Latency 1, throughput 2, field 01. | Keep the [Cortex-M52 r0p3 optimization guide](https://support.arm.com/documentation/107730/0003)'s throughput and slot field together even though they may look unusual; the M55 table uses a different field. |
| MOV forms | MOV (immediate) (T1): 1, 2, 11; register and shifted-register forms: 1, 1, 01. A separate MOV (T1) row prints 1(4), 2(1/4), 11(00). | The note says MOV PC, Rm can only be single-issued. |
| Branches | B (T2): 1(2), throughput 2, 11; B (T1): 1(2), throughput 1, 11. CBZ/CBNZ and BXNS: 3, 1, 00; BLX/BLXNS: 3, 1, 01; the LR destination BX row prints 3(2), 1, 11. | The LR row's note attributes reduced latency to late forwarding. Note 3's conditional-branch pairing sentence is internally unclear; do not derive a branch-pair recipe from it. See [branch timing and prediction guidance](techniques-cortex-m-branch-prediction.md) for M52 branch timing context. |
| MUL (T1), single narrow loads and stores | MUL: 2, 1, 01; listed single loads/stores: 2, 1, 01. | Use the exact operand and encoding row; these properties do not establish that adjacent operations can pair. |
| Narrow multiple transfers: LDM/POP, STM/PUSH | N + 1 latency, 1/N throughput, field 00. | Here N = num_regs; observe the multiple-transfer cost and do not treat these as dual-issue candidates. |
| NOP (T1) and IT (T1) | Latency 1, throughput 2, field 11. | The entries describe existing instruction behavior, not a reason to insert NOPs. |

These are representative eligibility and timing rows, not a complete pairing recipe. Check the assembled hot path, dependencies, flags, PC/SP effects, and legal pairing rules. Expected throughput improvements are an inference to measure on the workload; the Cortex-M52 r0p3 optimization guide reports no speedup. [Cortex-M52 r0p3 optimization guide, §§3.2–3.8, pp. 16–30](https://support.arm.com/documentation/107730/0003)