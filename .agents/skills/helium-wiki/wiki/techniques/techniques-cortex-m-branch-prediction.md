---
id: techniques-cortex-m-branch-prediction
title: Use Cortex-M52, Cortex-M55, and Cortex-M85 branch timing and prediction guidance
type: techniques
architectures:
  - armv8.1-m
  - cortex-m52
  - cortex-m55
  - cortex-m85
tags:
  - branch-prediction
  - low-overhead-branch
  - instruction-scheduling
  - asm
  - processor-initialization
  - c
confidence: source-reported
reproducibility: snippet
related:
  - techniques-armv81m-feature-initialization
  - techniques-helium-predication-and-tail-handling
  - techniques-helium-instruction-interleaving
  - techniques-cortex-m-latency-aware-scheduling
  - techniques-cortex-m85-latency-aware-scheduling
  - techniques-cortex-m-thumb-dual-issue
  - techniques-helium-low-overhead-loop-alignment
sources:
  - https://developer.arm.com/community/arm-community-blogs/b/architectures-and-processors-blog/posts/armv8_2d00_m-based-processor-software-development-hints-and-tips
  - https://support.arm.com/documentation/102692/0101
  - https://support.arm.com/documentation/107950/0100
  - https://support.arm.com/documentation/107730/0003
  - https://developer.arm.com/community/arm-research/b/articles/posts/making-helium-bringing-amdahl-s-law-to-heel
aliases:
  - Cortex-M85 CCR BP
  - Cortex-M55 r1 static branch prediction
  - ACTLR DISLOBR
  - Cortex-M55 branch latency
  - Cortex-M55 low-overhead loop timing
  - Cortex-M52 branch instruction timing
  - Cortex-M52 low-overhead loop timing
  - WLS DLS LE loop counter
  - Armv8.1-M low-overhead loop control
  - low-overhead loop interrupt recovery
  - Branch Future hint fallback
  - BFX BFL branch hint use cases
  - executable branch fallback after interrupt
prerequisites:
  - Identify the Cortex-M core and revision before changing prediction controls.
  - Make register changes from suitable privileged initialization code.
---

Use this page when evaluating branch timing, loop control, or prediction settings on Cortex-M52 r0p3, Cortex-M55 r1p1, or Cortex-M85 r1p0. Branch controls and timing depend on the core and revision, so follow the matching core-specific section. Reported instruction timings are source data, not application measurements.

## Armv8.1-M low-overhead loop control

For suitable scalar or vector loops, Armv8.1-M provides `WLS` (While Loop Start), `DLS` (Do Loop Start), and `LE` (Loop End). `WLS` places the iteration count in `LR` and skips the loop body when the count is zero. `DLS` provides do-loop semantics and is appropriate only when the caller can guarantee at least one iteration. `LE` tests the count in `LR` and branches to the loop start when another iteration remains. Since `LR` normally holds a function's return address, preserve the return address and calling-convention requirements before using it as a loop counter. [Making Helium: Bringing Amdahl's law to heel (4/4)](https://developer.arm.com/community/arm-research/b/articles/posts/making-helium-bringing-amdahl-s-law-to-heel)

Loop setup and the first iteration are distinct from steady-state loop control. An implementation may cache the loop start/end information supplied by `LE`; in steady state, it can take the loop branch before fetching `LE`. This loop-control information is distinct from instruction and data caches. If an interrupt or cache flush discards the cached loop information, execution can reach and re-execute the architectural `LE` instruction to repopulate it. Keep `LE` on the loop's recovery path, and do not mask interrupts just to use a low-overhead loop. The recovery mechanism avoids requiring separate preservation of loop start/end addresses; ordinary exception-state handling still applies. [Making Helium: Bringing Amdahl's law to heel (4/4)](https://developer.arm.com/community/arm-research/b/articles/posts/making-helium-bringing-amdahl-s-law-to-heel)

Treat these forms as target-supported loop-control options rather than a portable zero-overhead guarantee. For MVE element-count loops, see [Helium predication and tail handling](techniques-helium-predication-and-tail-handling.md); layout requirements are implementation-specific, as described in [Helium low-overhead loop alignment](techniques-helium-low-overhead-loop-alignment.md).

## Branch Future hints and executable fallbacks

Armv8.1-M Branch Future instructions provide advance information about an upcoming transfer and its branch point to implementation branch-information hardware. Unlike a fixed architectural delay slot, the hint lets an implementation use the lead time available in its own pipeline. `BFX` is the exchange form used for return-style transfers; `BFL` is the link form used for call-style transfers. [Making Helium: Bringing Amdahl's law to heel (4/4)](https://developer.arm.com/community/arm-research/b/articles/posts/making-helium-bringing-amdahl-s-law-to-heel)

Place a hint only when its destination and branch-point contract are already valid and stable. A return address already held in `LR` can provide early notice for a return; do not move the hint across code that changes the destination. Keep the ordinary branch at the specified branch point as an executable fallback: an interrupt can flush cached hint state between the hint and that point, so control flow must remain correct when the hint has no effect. [Making Helium: Bringing Amdahl's law to heel (4/4)](https://developer.arm.com/community/arm-research/b/articles/posts/making-helium-bringing-amdahl-s-law-to-heel)

Branch Future is optional, and an implementation may treat it as a NOP. The public processor guides document NOP behavior for Cortex-M52 r0p3, Cortex-M55 r1p1, and Cortex-M85 r1p0; the architectural motivation above is not evidence that these revisions accelerate code with Branch Future. Preserve the ordinary branch behavior and do not add hints on these cores in expectation of acceleration. [Arm China Cortex-M52 Processor Software Optimization Guide, §§2.2–3.2, pp. 11–17](https://support.arm.com/documentation/107730/0003); [Arm Cortex-M55 Processor Software Optimization Guide, §3.2, pp. 17–18](https://support.arm.com/documentation/102692/0101); [Arm Cortex-M85 Processor Software Optimization Guide, §3.2, p. 14](https://support.arm.com/documentation/107950/0100) See [Helium instruction interleaving](techniques-helium-instruction-interleaving.md) for optional-hint scheduling guidance.

## Cortex-M85 and Cortex-M55 branch-prediction controls

Branch-prediction controls differ between Cortex-M85 and Cortex-M55 revisions. Use the control that applies to the exact core and revision; the settings below are source-reported and should not be generalized across revisions.

The [Armv8.1-M software development hints and tips article](https://developer.arm.com/community/arm-community-blogs/b/architectures-and-processors-blog/posts/armv8_2d00_m-based-processor-software-development-hints-and-tips) says branch prediction is disabled by default on Cortex-M85. Enable it by setting `CCR.BP` and then synchronizing instruction execution with DSB and ISB. With CMSIS-CORE, the example is:

```c
SCB->CCR |= SCB_CCR_BP_Msk;
__DSB();
__ISB();
```

Cortex-M55 r1 supports limited static branch prediction by reusing low-overhead branch hardware, which the [Armv8.1-M software development hints and tips article](https://developer.arm.com/community/arm-community-blogs/b/architectures-and-processors-blog/posts/armv8_2d00_m-based-processor-software-development-hints-and-tips) says may help in a few cases. On r1p0, it is disabled by default and can be enabled by clearing bit 5 (`DISLOBR`) in `ACTLR`. On r1p1, that bit is cleared by default. This is distinct from Cortex-M85's `CCR.BP` control, and the Armv8.1-M software development hints and tips article makes no corresponding claim for Cortex-M55 r0.

Check the core revision and the intended low-overhead branch behavior before changing `ACTLR`; do not assume that enabling this limited M55 behavior has the same effect as setting M85 `CCR.BP`. See [techniques-armv81m-feature-initialization](techniques-armv81m-feature-initialization.md) for the separate low-overhead branch enablement setting.

## Cortex-M52 r0p3, Issue 03 branch timing

The Cortex-M52 r0p3 optimization guide reports branch instruction behavior and timing but gives no Cortex-M52 CCR.BP or ACTLR configuration sequence. Keep the branch-prediction controls above scoped to the M85 and M55 sources that describe them. On M52, use the timing rows for the emitted encoding and loop condition; latency and throughput are instruction-table values, not measured loop costs. [Arm China Cortex®-M52 Processor Software Optimization Guide, document 107730_0003_03_en, §§2.2–3.2, pp. 11–17](https://support.arm.com/documentation/107730/0003)

The pipeline changes the PC early for common conditional, indirect, and BX LR cases. For a load-multiple operation that branches, the Cortex-M52 r0p3 optimization guide says the PC is loaded from memory first so target instructions can be fetched while the remaining registers load; this is a pipeline description, not an architectural license to reorder program-visible loads. [Cortex-M52 r0p3 optimization guide, §2.2, p. 12](https://support.arm.com/documentation/107730/0003)

| M52 branch or loop instruction | Source-reported timing | Qualification |
| --- | --- | --- |
| 32-bit Branch Future family: BF, BFCSEL, BFL, BFLX, BFX | Latency 1, throughput 1. | These instructions act as NOPs; the timing does not establish a branch-prediction benefit. |
| 32-bit immediate B (T3/T4) | Latency 1(2), throughput 1(1/2). | For the documented backward-branch condition, subsequent branches are predicted taken and latency reduces to 0 when the branch is implied. This conditional entry does not make every backward branch or first traversal free. |
| 32-bit immediate BL (T1) | Latency 2, throughput 1/2. | Keep this row distinct from immediate B. |
| Low-overhead loop setup/count: DLS, DLSTP, LCTP | Latency 1, throughput 1. | Individual instruction timing, not a per-iteration loop cost. |
| Low-overhead loop end: LE (T1/T2), LETP (T3) | Latency 3, throughput 1/3. | Do not interpret this as three added cycles on every optimized-loop iteration. |
| While-loop: WLS (T1), WLSTP (T3) | Latency 1(3), throughput 1(1/3). | The [Cortex-M52 r0p3 optimization guide](https://support.arm.com/documentation/107730/0003) attributes the 3-cycle latency penalty to the case where the while loop is not executed. |
| 16-bit immediate B (T2) and B (T1) | Both have latency 1(2), issue field 11; T2 throughput is 2 and T1 throughput is 1. | For the backward-branch condition, the same implied-branch note applies. |
| 16-bit CBZ/CBNZ, BXNS, BLX/BLXNS | CBZ/CBNZ and BXNS: latency 3, throughput 1, field 00. BLX/BLXNS: 3, 1, field 01. | Check the exact branch form; these rows have distinct issue eligibility. |
| 16-bit BX with destination LR | The row prints latency 3(2), throughput 1, field 11. | The note attributes the reduced latency to late forwarding for branch-exchange instructions using LR. |

The branch-pairing note on p. 17 is internally unclear: it says a conditional branch can be dual-issued as the first instruction in a pair only if the first instruction is an unconditional immediate B (T2). Preserve the wording as an ambiguity and do not derive a pairing recipe. The Cortex-M52 r0p3 optimization guide also says immediate branches, low-overhead-loop instructions, and Branch Future NOPs may overlap tick 3 of a preceding MVE instruction, subject to pipeline hazards; a scalar cannot overlap the final vector instruction marked with implicit LE. See [Helium instruction interleaving](techniques-helium-instruction-interleaving.md) for the M52-specific overlap constraints. [Cortex-M52 r0p3 optimization guide, §3.2, pp. 16–17; §4.1, p. 45](https://support.arm.com/documentation/107730/0003)

The [Cortex-M52 r0p3 optimization guide](https://support.arm.com/documentation/107730/0003) does not state an M52 branch-target alignment rule or measured speedup for these branch timings. Inspect the actual branch encoding and loop behavior on the target workload.

## Cortex-M55 r1p1 branch and loop timing

The Cortex-M55 r1p1 optimization guide provides the following instruction timing rows. Latency is the minimum delay observed by a dependent operation, while throughput is the maximum instruction rate for that group; neither is a direct measure of an optimized loop's total cycle cost. These timing figures apply to the documented M55 revision and do not establish M85 branch timing. [Arm Cortex-M55 Processor Software Optimization Guide, §3.1, p. 16; §3.2, pp. 17–18](https://support.arm.com/documentation/102692/0101)

| Instruction group or condition | Source-reported latency and throughput | Interpretation |
| --- | --- | --- |
| 32-bit Branch Future family (`BF`, `BFCSEL`, `BFL`, `BFLX`, `BFX`) | Latency 1, throughput 1; the listed family acts as a NOP. | The row does not establish a branch-prediction benefit for these instructions. |
| 32-bit immediate `B` | Table entries show latency `1(2)` and throughput `1(1/2)`. For the backward-branch condition in Note 2, subsequent branches are predicted taken and latency reduces to 0 when the branch is implied. | The zero-latency statement is conditional; it does not make every backward branch free. Preserve the exact encoding and sequence condition when applying it. |
| 32-bit immediate `BL` | Latency 2, throughput 1/2. | Keep this row distinct from the immediate `B` entries. |
| Low-overhead loop setup/count instructions `DLS`, `DLSTP`, `LCTP` | Latency 1, throughput 1. | These are individual instruction timings, not per-iteration overhead claims. |
| Low-overhead loop end instructions `LE`, `LETP` | Latency 3, throughput 1/3. | Do not interpret the table entry as three extra cycles on every optimized-loop iteration. |
| While-loop instructions `WLS`, `WLSTP` | Table entries show latency `1(3)`, throughput `1(1/3)`. If the while loop is not executed, the branch causes a 3-cycle latency penalty. | The 3-cycle penalty is specifically stated for the not-executed case. |
| 16-bit immediate `B` (`T2`) | Latency `1(2)`, throughput 2, dual-issue field `11`. The backward-branch note describes the conditional zero-latency case above. | Dual issue and the conditional branch prediction are separate table properties; check the actual encoding and context. |
| 16-bit register branches | `BX (T1)`: latency 2, throughput 1, dual-issue field `11`. `CBZ`/`CBNZ`: 3, 1, `00`. `BXNS`: 3, 1, `00`. `BLX`/`BLXNS`: 3, 1, `01`. | These are distinct encodings and dual-issue classes. The [Cortex-M55 r1p1 optimization guide](https://support.arm.com/documentation/102692/0101)'s note says a branch exchange using LR has reduced latency through late forwarding; apply it only to the applicable LR form. |

The following row is also printed in Table 3-2: “Branch, register (with destination LR)” `BX (T1)`, latency `3(2)`, throughput 1, dual-issue field `11`, Note 3. Note 3 has unclear wording about a conditional branch being dual-issued “as the first instruction in a pair” only if the first instruction is an unconditional immediate `B (T2)`. Keep the parenthetical row value and note scoped to the Cortex-M55 r1p1 optimization guide's wording; do not turn the sentence into a branch-pairing recipe. [Cortex-M55 r1p1 optimization guide, §3.2, Tables 3-1 and 3-2, pp. 17–18](https://support.arm.com/documentation/102692/0101)

## Cortex-M85 r1p0 branch prediction and alignment

For Cortex-M85 r1p0, the pipeline description says successful branch predictions remove the penalty associated with taken branches and allow predicted-target instructions to follow immediately. A nominal misprediction resolved in EX2 and provided to prefetch from EX3 has a 7-cycle branch latency; the Cortex-M85 r1p0 optimization guide says pipeline performance features often reduce this by one or more cycles. Treat seven cycles as the nominal case described, not the cost of every taken branch or a measured application penalty. [Arm Cortex-M85 Processor Software Optimization Guide, r1p0, Issue 01 (26 April 2023), §2.2, p. 12](https://support.arm.com/documentation/107950/0100)

The M85 instruction tables separately report minimum branch-instruction timings: the listed 32-bit immediate `B`/`BL`, low-overhead-loop instructions, and 16-bit branch forms report latency 1, throughput 1, and slot field `11`. Branch Future instructions also report 1/1/`11` and act as NOPs. These table values describe instruction timing and eligibility; they are not the conditional prediction or misprediction penalty, nor a measured call or loop cost. [Cortex-M85 r1p0 optimization guide, §3.2, p. 14](https://support.arm.com/documentation/107950/0100)

The Cortex-M85 r1p0 optimization guide reports that placing multiple branches in the same aligned 64-bit memory region can burden branch-prediction logic and affect performance negatively. Avoid that packing when doing so does not harm code density or performance. It also prefers branch targets, including subroutine entries, on 64-bit boundaries to use instruction-fetch bandwidth. Treat this as a layout preference to evaluate against padding and code density, not a compulsory alignment or guaranteed gain. This 8-byte target preference supplements the separate 4-byte alignment guidance for low-overhead branches. [Cortex-M85 r1p0 optimization guide, §4.5, p. 45](https://support.arm.com/documentation/107950/0100)

In the M85 MVE overlap examples, `LE` and `LETP` have no issue cost and do not influence LDST/SIMD overlap regardless of placement. That does not remove resource conflicts across a loop backedge: the Cortex-M85 r1p0 optimization guide shows that a Group A vector add at the end and beginning of successive loop bodies cannot overlap merely because `LE` lies between them. Check the last and first operations across the backedge as well as the loop-control instruction. [Cortex-M85 r1p0 optimization guide, §4.10, p. 47](https://support.arm.com/documentation/107950/0100)

## Cortex-M55 r1p1 overlap with MVE instructions

The Cortex-M55 r1p1 optimization guide says MVE instructions execute as two 64-bit micro-operations (“ticks”). Whether a following instruction overlaps the older instruction's second tick is decided in E0 and depends on resources and data availability; an E0 hazard stall prevents overlap. For Cortex-M55, the listed scalar classes that may overlap the preceding vector instruction's second tick include immediate branches (`B`, `BL`, `CB[N]Z`), low-overhead-loop instructions, and Branch Future instructions (which are NOPs). This describes possible overlap, not a guarantee for every loop body. [Arm Cortex-M55 Processor Software Optimization Guide, r1p1, Issue 03, §4.1, p. 46](https://support.arm.com/documentation/102692/0101)

The Cortex-M55 r1p1 optimization guide lists constraints on this overlap: an arithmetic instruction cannot overlap a vector load/store that writes back its base to a scalar register; an instruction that checks the stack limit does not overlap; a scalar cannot overlap a vector instruction carrying an implicit `LE` (the last instruction in a low-overhead loop); and scalar/vector dependencies make overlap unlikely. Apply these conditions before inferring loop timing from the instruction tables. [Arm Cortex-M55 Processor Software Optimization Guide, r1p1, Issue 03, §4.1, p. 46](https://support.arm.com/documentation/102692/0101)
