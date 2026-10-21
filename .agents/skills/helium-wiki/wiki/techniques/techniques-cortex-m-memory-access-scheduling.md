---
id: techniques-cortex-m-memory-access-scheduling
title: Schedule Cortex-M52 and Cortex-M55 memory accesses by encoding and resource use
type: techniques
architectures:
  - cortex-m52
  - cortex-m55
tags:
  - instruction-scheduling
  - data-placement
  - buffer-alignment
  - helium
  - mve
  - data-tcm
  - asm
confidence: source-reported
reproducibility: concept
related:
  - hardware-dtcm-scatter-gather-banks
  - hardware-itcm-instruction-data-contention
  - techniques-helium-instruction-interleaving
  - techniques-cortex-m-latency-aware-scheduling
  - patterns-helium-dtcm-dma-pipeline
sources:
  - https://support.arm.com/documentation/102692/0101
  - https://support.arm.com/documentation/107730/0003
  - https://github.com/Arm-Examples/Helium-Optimization/blob/bc9dfb67be64a2a5ee8e5325910a3f060a593863/General_optimizations/processor_specific.md#L46-L57
aliases:
  - Cortex-M55 gather forwarding penalty
  - Cortex-M55 multi-register transfer timing
  - MVE memory throughput
  - Cortex-M55 scatter gather scheduling
  - Cortex-M55 memory access latency
  - Cortex-M52 scalar and vector memory timing
  - Cortex-M52 gather scatter writeback
  - Cortex-M52 DTCM store-to-read hazard
  - Cortex-M55 DTCM producer-consumer bank placement
evidence_basis: Instruction timings and hazards are source-reported for Cortex-M52 r0p3 and Cortex-M55 r1p1, Issue 03. Layout and scheduling advice is an implementation implication; the Cortex-M52 and Cortex-M55 optimization guides report no workload speedup.
---

Use this page when reviewing scalar or MVE memory operations on Cortex-M52 r0p3 or Cortex-M55 r1p1. Match transfer timing, pipeline, encoding, forwarding, and memory-wait guidance to the exact core; instruction tables do not measure the configured memory system.

## Cortex-M55 r1p1, Issue 03

## Scalar load and store timing

The ordinary scalar load rows (including the listed narrow and wide `LDR` forms) report latency 2 and throughput 1 instruction per cycle. `LDRD` has the same timing. Ordinary scalar stores and `STRD` also report latency 2 and throughput 1. These are instruction-table figures; a read can still wait for data in the memory system. [Cortex-M55 r1p1 optimization guide, §3.6, Tables 3-9–3-10, pp. 27–29; §3.7, Tables 3-11–3-12, pp. 29–30](https://support.arm.com/documentation/102692/0101)

For the listed load-multiple and store-multiple instructions, the tables report latency `N + 1` and throughput `1/N`. The associated note says Cortex-M55 supports two 32-bit accesses per cycle and defines `N = floor((num_regs + 1) / 2)`. The load rows include `LDM`, `LDMIA`, `LDMFD`, `LDMDB`, and `LDMEA`; the store rows include `STM`, `STMIA`, `STMEA`, `STMDB`, and `STMFD`. The 16-bit load table also lists `POP`, and the 16-bit store table lists `PUSH`. Apply the formula to the transfer rows as printed; it is not a general promise that every transfer will proceed at that rate regardless of memory response or other hazards. [Cortex-M55 r1p1 optimization guide, §3.6, Tables 3-9–3-10 and note 1, pp. 27–29; §3.7, Tables 3-11–3-12 and note 1, pp. 29–31](https://support.arm.com/documentation/102692/0101)

The FP load rows list `VLDR` T1–T3 at latency 2 and throughput 1. The store rows distinguish `VSTR` T2/T3 at latency 1 and throughput 1 from T1 at latency 2 and throughput 1. Check the form actually emitted; these encoding rows do not establish that the forms are interchangeable. [Cortex-M55 r1p1 optimization guide, §3.9, Table 3-15, pp. 32–33](https://support.arm.com/documentation/102692/0101)

The FP multiple-transfer rows require extra care. Table 3-15 prints latency `(N/2) + 1` and throughput `1/((N/2) + 1)` for `VLDM`, `VLLDM`, and `VSCCLRM` (T1), and the same figures for `VLSTM` and `VSTM` (T1). Its note 2 says Cortex-M55 supports two 32-bit accesses per cycle and defines `N` for **single-precision store-multiple** instructions as `floor((num_regs + 1) / 2)` and for **double-precision store-multiple** instructions as `num_regs`, while the table also includes load and context instructions. Preserve the formula and scope as printed; the note does not define how to reinterpret `N` for every listed load row. Do not silently replace the formula or extend the store-specific definition to those loads. [Cortex-M55 r1p1 optimization guide, §3.9, Table 3-15, pp. 32–33; note 2, p. 34](https://support.arm.com/documentation/102692/0101)

## MVE load encodings

| Load encoding group | Reported latency | Reported throughput | Pipeline and qualification |
| --- | --- | --- | --- |
| Contiguous `VLDRB`, `VLDRH`, `VLDRW` T1, T2, T5, T6, T7 | 1 | 1/2 instruction/cycle | LS pipe |
| Contiguous `VLD2`, `VLD4` T1 | 2 | 1/2 instruction/cycle | LS pipe |
| Gather `VLDRB/H/W/D (vector)` T1–T4 | `(64 / esize) + 1` | `esize / 64` | LS pipe; `esize` is the variant's element size (8, 16, or 32 bits). T1 has an additional forwarding note described below. |
| Gather T5 | `1(2)` | 1/2 instruction/cycle | I and LS pipes; the condition for the parenthesized latency is not explained by the table note. |
| Gather T6 | 2 | 1/4 instruction/cycle | I and LS pipes |

The T1 gather note says gather loads cannot read forwarded data, so the **preceding instruction's latency** receives an additional 4 cycles when its result feeds that gather. The T2–T4 rows carry the element-size note but not this +4 forwarding note; do not transfer the T1 penalty to those encodings. [Cortex-M55 r1p1 optimization guide, §3.14, Table 3-20 and notes 1–2, p. 44](https://support.arm.com/documentation/102692/0101)

For the T5 and T6 gather rows, the Cortex-M55 r1p1 optimization guide says the first cycle uses LS and the second uses I. For full overlap, it says the older instruction should use a non-LS pipe and the younger instruction a non-I pipe. Table 3-20 prints T5 latency as `1(2)`, but note 3 explains the pipeline order, not the condition for that parenthesized value. Preserve the table's notation and treat its condition as unspecified here rather than assigning it to an assumed case. [Cortex-M55 r1p1 optimization guide, §3.14, Table 3-20 and note 3, p. 44](https://support.arm.com/documentation/102692/0101)

## MVE store and scatter rows

The scatter-store rows for `VSTRB/H/W/D (vector)` T1–T4 report latency 1, throughput `esize / 64`, and the LS pipe. T5 reports latency 2 and throughput 1/2; T6 reports latency 2 and throughput 1/4, both using I and LS. The contiguous `VSTRB/H/W` T1/T2 rows report latency 1 and throughput 1/2 in LS; T5/T6/T7 rows have the same reported timing. `VST2` and `VST4` T1 also report latency 1 and throughput 1/2 in LS. [Cortex-M55 r1p1 optimization guide, §3.15, Table 3-21 and notes 1–2, p. 45](https://support.arm.com/documentation/102692/0101)

For the store rows covered by notes 1 and 2, the Cortex-M55 r1p1 optimization guide says that when stored data depends on a preceding instruction, the preceding instruction's latency is not affected. This is a specific producer-latency statement; it does not mean the store has no pipeline, memory, ordering, or throughput constraints. Keep the data dependency and other applicable hazards when evaluating a schedule. [Cortex-M55 r1p1 optimization guide, §3.15, Table 3-21 and notes 1–2, p. 45](https://support.arm.com/documentation/102692/0101)

## Alignment, memory waits, and overlap hazards

The load/store address stage decomposes an unaligned access into constituent aligned requests; while those subrequests execute, the original instruction stalls in DPU EX. The read-data stage stalls the DPU until cache or TCM data is available, and a cache miss can require an M-AXI access. Device-memory accesses cannot be speculative and occur later, after the instruction is committed. These behaviors do not supply a single fixed cycle cost for every unaligned or delayed read. As an implementation implication, prefer natural alignment and contiguous access where the data format and program semantics permit, then measure the actual memory path. [Cortex-M55 r1p1 optimization guide, §2.2, pp. 14–15](https://support.arm.com/documentation/102692/0101)

MVE overlap is decided using resource and data availability. In particular, arithmetic instructions cannot overlap with vector loads or stores that write back the base address to a scalar register; a dependency between a scalar and vector instruction also makes overlap unlikely. Do not infer that otherwise independent operations will overlap just because they use different pipelines. [Cortex-M55 r1p1 optimization guide, §4.1, p. 46](https://support.arm.com/documentation/102692/0101)

For the separate one-cycle same-bank vector-load penalty in M55 r1p1 TCM, see [hardware-dtcm-scatter-gather-banks](../hardware/hardware-dtcm-scatter-gather-banks.md). General MVE interleaving limits are covered by [techniques-helium-instruction-interleaving](techniques-helium-instruction-interleaving.md). Confirm the emitted encoding, addresses, dependencies, and memory placement before changing a hot path; preserve access and ordering semantics. The [Cortex-M55 r1p1 optimization guide](https://support.arm.com/documentation/102692/0101) does not report measured gains from these schedule or layout changes.


## Cortex-M52 r0p3, Issue 03

On M52, distinguish instruction-table latency and throughput from the response time of the configured memory system. An unaligned request is split into aligned LSU requests and the returned data is recombined; the Cortex-M52 r0p3 optimization guide gives no single fixed unaligned-access penalty. The LSU stalls the DPU until read data is available from M-AXI or TCM. Device-memory accesses are selected after commit and cannot be speculative. Natural alignment is therefore a candidate layout choice when it preserves the data format and semantics, not a source-reported cycle guarantee. [Arm China Cortex®-M52 Processor Software Optimization Guide, document 107730_0003_03_en, §2.2, pp. 12–14](https://support.arm.com/documentation/107730/0003)

| M52 scalar transfer group | Latency / throughput | Notes |
| --- | --- | --- |
| Basic, subword, acquire, exclusive, release loads and stores | 2 / 1 | Preserve atomic, acquire/release, and access-width semantics. |
| LDRD and STRD | 3 / 1/2 | The [Cortex-M52 r0p3 optimization guide](https://support.arm.com/documentation/107730/0003) reports one 32-bit access per cycle. |
| LDM/STM multiple transfers, including narrow LDM/POP and STM/PUSH | N + 1 / 1/N | N is the number of registers; the core supports one 32-bit access per cycle. Narrow single loads/stores have issue field 01; multiple-transfer rows have field 00. |

For scalar floating-point transfers, the table reports VLDR T2/T3 at latency 2 and throughput 1, VLDR T1 at 3 and 1/2, VSTR T2/T3 at 1 and 1, and VSTR T1 at 2 and 1/2. The VLDM, VLLDM, VSCCLRM, VLSTM, and VSTM multiple rows print latency N + 1 and throughput 1/N. The accompanying note specifies one 32-bit access per cycle and defines N for single-precision store-multiple as num_regs and double-precision store-multiple as 2 × num_regs. It is a store-specific definition even though the table also has load and context rows; the note does not clearly define N for every load or context row. [Cortex-M52 r0p3 optimization guide, §3.9, pp. 31–33](https://support.arm.com/documentation/107730/0003)

| M52 vector-memory form | Source-reported latency / throughput | Pipeline and qualification |
| --- | --- | --- |
| Continuous VLDRB T1/T5, VLDRH T2/T6, VLDRW T7; deinterleaving VLD2/VLD4 T1 | 2 / 1/4 | LS. The figures are per-instruction timings, not bytes transferred per instruction. |
| Gather VLDRB T1, element size 8 / 16 / 32 bits | 17 / 1/16; 9 / 1/8; 2 / 1/4 | LS and VF. |
| Gather VLDRH T2, element size 16 / 32 bits | 9 / 1/8; 2 / 1/4 | LS and VF. |
| Gather VLDRW T3 and VLDRD T4 | 2 / 1/4 | LS and VF. |
| Gather VLDRW T5 and VLDRD T6, non-writeback / writeback | 2 / 1/4; 9 / 1/8 | LS and VF; the T5/T6 writeback operation executes in VF. |
| Continuous VSTRB/H/W T1/T2/T5/T6/T7; interleaving VST2/VST4 T1 | 2 / 1/4 | LS. The [Cortex-M52 r0p3 optimization guide](https://support.arm.com/documentation/107730/0003) table labels the first group “Continuous Vector Load Store” despite listing store mnemonics. |
| Scatter VSTRB T1, element size 8 / 16 / 32 bits | 17 / 1/16; 9 / 1/8; 2 / 1/4 | LS and VF. |
| Scatter VSTRH T2, element size 16 / 32 bits | 9 / 1/8; 2 / 1/4 | LS and VF. |
| Scatter VSTRW T3 and VSTRD T4 | 2 / 1/4 | LS and VF. |
| Scatter VSTRW T5 and VSTRD T6, non-writeback / writeback | 2 / 1/4; 9 / 1/8 | LS and VF; the T5/T6 writeback operation executes in VF. |

The table values do not imply that every data layout, access, or memory target reaches the listed maximum rate. The Cortex-M52 r0p3 optimization guide gives no TCM bank count or same-bank penalty, no separate ITCM placement recommendation, no M55-style extra forwarding latency for a T1 gather, and no M55 dependent-store producer-latency claim. Keep those other-core details in their existing scoped sections. [Cortex-M52 r0p3 optimization guide, §§3.6–3.7, pp. 26–29; §3.9, pp. 31–33; §§3.14–3.15, pp. 43–44](https://support.arm.com/documentation/107730/0003)

M52 also excludes all scalar loads and stores from overlap with vector instructions; a scalar/vector dependency is another overlap risk. Gather/scatter instructions may use both LS and VF, and T5/T6 writeback uses VF, so address updates are not free. Evaluate any alternative against address-register pressure, dependencies, full instruction cost, and preserved memory semantics. See [Helium instruction interleaving](techniques-helium-instruction-interleaving.md) for overlap conditions. These are implementation implications from source-reported behavior, not measured workload gains. [Cortex-M52 r0p3 optimization guide, §4.1, p. 45](https://support.arm.com/documentation/107730/0003)

## DTCM store-to-read placement on Cortex-M52 and Cortex-M55

The [Processor specific details article](https://github.com/Arm-Examples/Helium-Optimization/blob/bc9dfb67be64a2a5ee8e5325910a3f060a593863/General_optimizations/processor_specific.md) reports that a read immediately following a write to the same DTCM bank may be delayed on Cortex-M52 and Cortex-M55. This is separate from the M55/M85 opportunity to overlap independent small loads that map to different banks, and it does not establish that Cortex-M52 has the same two-access scatter/gather capability. When the store and following read access independent buffers, evaluate layout or offsets so they reach different banks, then verify the actual placement and complete kernel on the target. Preserve any same-address data dependency rather than trying to move its read to another bank. The Processor specific details article does not provide a bank width or a quantitative penalty for this condition; do not infer Cortex-M52 bank geometry from another core.

The [Processor specific details article](https://github.com/Arm-Examples/Helium-Optimization/blob/bc9dfb67be64a2a5ee8e5325910a3f060a593863/General_optimizations/processor_specific.md) also discusses ITCM contention and placement, but Cortex-M55/M85 ITCM code-versus-data guidance is already covered by [ITCM instruction and data contention](../hardware/hardware-itcm-instruction-data-contention.md). See [TCM workspaces and DMA pipelines](../patterns/patterns-helium-dtcm-dma-pipeline.md) for broader data-placement tradeoffs.
