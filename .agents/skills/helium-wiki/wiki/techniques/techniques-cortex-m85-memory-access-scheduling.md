---
id: techniques-cortex-m85-memory-access-scheduling
title: Schedule Cortex-M85 memory accesses using encodings, dependencies and alignment
type: techniques
architectures:
  - cortex-m85
tags:
  - instruction-scheduling
  - buffer-alignment
  - data-placement
  - helium
  - mve
  - asm
confidence: source-reported
reproducibility: concept
related:
  - techniques-cortex-m85-latency-aware-scheduling
  - techniques-cortex-m85-floating-point-scheduling
  - techniques-helium-instruction-interleaving
  - hardware-dtcm-scatter-gather-banks
  - hardware-itcm-instruction-data-contention
sources:
  - https://support.arm.com/documentation/107950/0100
aliases:
  - Cortex-M85 load and store scheduling
  - M85 memory access latency
  - M85 gather and scatter scheduling
  - Cortex-M85 store data forwarding
  - Cortex-M85 memory alignment
evidence_basis: Arm Cortex-M85 r1p0, Issue 01 pipeline description, memory instruction tables, and dependency/alignment guidance. Timing is source-reported architectural guidance, not measured workload performance.
---

For hot Cortex-M85 r1p0 code, schedule memory operations from the exact encoding and the value each instruction consumes. Scalar, scalar FP, continuous MVE, gather/scatter, and interleaving forms have different timing and resource properties even when their source-level purpose looks similar. These guide values describe minimum dependent latency and peak throughput, not whole-loop cycle counts; cache misses and misalignment can add stalls. [Arm Cortex-M85 Processor Software Optimization Guide, r1p0, Issue 01 (26 April 2023), sections 2.2 and 3.1, pages 12-13](https://support.arm.com/documentation/107950/0100)

## LSU capacity and data-return timing

The LSU can accommodate two loads, two stores, or one load and one store per cycle. The stated aggregate load and store bandwidths are each 64 bits per cycle. Word data from TCM or L1 D-cache returns in two cycles, and subword data in three cycles. A data-cache miss stalls the pipeline, so table throughput is not a prediction for external-memory misses or misaligned requests. [Cortex-M85 r1p0 optimization guide, section 2.2, page 12; section 3.1, page 13](https://support.arm.com/documentation/107950/0100)

## Check scalar integer encodings

The Cortex-M85 r1p0 optimization guide's scalar-load table reports these representative encoding-specific values. The dual-issue field meanings are `00` no dual issue, `01` slot 0 only, and `11` either slot; use the actual encoding rather than the mnemonic alone. [Cortex-M85 r1p0 optimization guide, section 3.6, pages 24-26; section 3.1, page 13](https://support.arm.com/documentation/107950/0100)

The scalar-load tables give base-register update latency as 1. This update latency is a distinct dependency from the loaded-data latency and does not change the consumer-specific adjustments below. [Cortex-M85 r1p0 optimization guide, section 3.6, pages 24-26](https://support.arm.com/documentation/107950/0100)

| Load form | Latency | Throughput | Dual-issue field |
| --- | ---: | ---: | --- |
| Word `LDR` T32 immediate/literal/register and T16 immediate/literal/register rows | 2 | 2 | 11 |
| `LDRD` T32 immediate/literal | 2 | 1 | 11 |
| `LDM`/`LDMDB` and T16 `LDM`/`POP` multiple | N+1 | 1/N | 00 |
| Word `LDA`/`LDAEX`/`LDREX` | 2 | 1 | 00 |
| Subword exclusive loads (`LDAB`, `LDAH`, `LDAEXB/H`, `LDREXB/H`) | 3 | 1 | 00 |
| T16 subword `LDRB`/`LDRH` immediate/register and `LDRSB`/`LDRSH` register rows | 3 | 2 | 11 |

For multiple-register rows, `N = floor((num_regs + 1) / 2)`, reflecting two 32-bit accesses per cycle in the Cortex-M85 r1p0 optimization guide's formula. The T32 subword-load row prints latency 3, throughput 3, and field `02`. That field is not defined alongside `00`, `01`, and `11` in section 3.1, and the row differs from the T16 subword throughput and stated LSU bandwidth. Treat the printed row as an unresolved source ambiguity; do not infer that three loads can issue per cycle or assign slot eligibility from `02`. [Cortex-M85 r1p0 optimization guide, sections 3.1 and 3.6, pages 13 and 24-26](https://support.arm.com/documentation/107950/0100)

For scalar stores, basic `STR`, `STRB`, and `STRH` rows list throughput 2 and field `11`; `STRD` lists throughput 1 and field `11`. Multiple `STM`/`STMDB`/`PUSH` rows list throughput `1/N` and field `00`, using the same `N` formula. `STREX` and its byte/halfword forms list throughput 1 and field `00`. Release stores (`STL`, `STLB`, `STLH`, and `STLEX` variants) list throughput `1/5` and field `00`; preserve their synchronization semantics rather than replacing them with ordinary stores to change timing. Store-result latency is generally unreported in these rows. [Cortex-M85 r1p0 optimization guide, section 3.7, pages 26-28](https://support.arm.com/documentation/107950/0100)

The scalar-store tables also state that base-register update latency is 1. Keep that address-update dependency distinct from the store-data dependency, whose timing can be adjusted as described later. [Cortex-M85 r1p0 optimization guide, section 3.7, pages 26-28](https://support.arm.com/documentation/107950/0100)

## Keep scalar FP memory forms distinct

Scalar FP loads `VLDR` T2/T3 list latency 2, throughput 2, and field `11`; T1 lists latency 2, throughput 1, and field `01`. `VLDM`/`VLLDM` list latency `N+1` and throughput `1/N`, with field `11(01)`. For multiple transfers, the Cortex-M85 r1p0 optimization guide defines `N = floor((num_regs+1)/2)` for single precision and `N = num_regs` for double precision. The Cortex-M85 r1p0 optimization guide's table note does not clearly explain the parenthetical issue-field condition, so do not assign an inferred condition to it. [Cortex-M85 r1p0 optimization guide, section 3.9, pages 29-31](https://support.arm.com/documentation/107950/0100)

For stores, `VSTR` T2/T3 lists throughput 2 and field `11`; T1 lists throughput 1 and field `01`. `VSTM`/`VLSTM` list throughput `1/N` and field `11(01)`, using the same precision-dependent `N`. These rows do not supply a store-result latency; do not derive one from the throughput. [Cortex-M85 r1p0 optimization guide, section 3.9, pages 29-31](https://support.arm.com/documentation/107950/0100)

## Distinguish continuous, gather, scatter, and interleaving MVE forms

| MVE form | Source-reported timing and resource | Scheduling detail |
| --- | --- | --- |
| Continuous vector `VLDRB/H/W` T1/T2/T5/T6/T7 | Load latency 2; throughput 1/2; LD group | Use the exact encoding and preserve load dependencies. |
| Interleaving `VLD2`/`VLD4` T1 | Latency 4; throughput 1/2; LD group | Its consumer timing differs from a continuous load. |
| Gather `VLDRB/H/W/D` T1/T2/T3 | Latency `(64/esize)+1`; throughput `esize/64`; LD group; base-update latency 7 | The listed `esize` values are 8, 16, or 32 depending on supported variant. The formula gives 9 and 1/8 for 8-bit, 5 and 1/4 for 16-bit, and 3 and 1/2 for 32-bit variants; it does not imply every encoding supports every size. |
| Gather T4/T5/T6 | Latency 3; throughput 1/2; LD group | Do not import Cortex-M55 T5/T6 mixed-pipe or forwarding penalties. |
| Scatter `VSTRB/H/W/D` T1/T2/T3 | Throughput `esize/64`; ST group; base-update latency 7 | The same supported-size caveat applies. The table gives no execution latency for these stores. |
| Scatter T4/T5/T6 | Throughput 1/2; ST group | Use the row for the exact form. |
| Continuous vector `VSTRB/H/W` T1/T2/T5/T6/T7 and interleaving `VST2`/`VST4` T1 | Throughput 1/2; ST group | Store-data producer adjustments differ between ordinary and interleaving stores. |

The MVE load/store tables report resource groups and variant-specific timing; they should not be collapsed into one generic “vector memory” cost. [Cortex-M85 r1p0 optimization guide, sections 3.14-3.15, pages 41-42](https://support.arm.com/documentation/107950/0100)

MVE store base-update latency is 1 unless a row's note says otherwise. The noted gather-load variants and scatter-store T1/T2/T3 variants above have base-update latency 7; do not replace those variant-specific values with the ordinary MVE-store value. [Cortex-M85 r1p0 optimization guide, sections 3.14-3.15, pages 41-42](https://support.arm.com/documentation/107950/0100)

## Schedule dependencies by consumer

Scalar FP and MVE consumers start execution two cycles after integer/LSU execution. That changes the delay observed by a dependent consumer without changing the producer's table latency. In the Cortex-M85 r1p0 optimization guide's examples, a dependent vector instruction immediately after a latency-2 vector load can issue without penalty; after a latency-4 vector load, it stalls two cycles. Integer consumers see correspondingly longer delays from FP/MVE producers of scalar integer values. [Cortex-M85 r1p0 optimization guide, section 4.2, pages 43-44](https://support.arm.com/documentation/107950/0100)

Store-data timing is also consumer-specific. An integer store-data consumer sees a producer latency two cycles shorter than the table value; a scalar store using data from a latency-2 load can issue immediately without stalls. This adjustment applies to store data, not a dependent address or base register. An ordinary vector store accepts vector-producer store data three cycles sooner than the tabulated producer latency, while a vector store immediately after vector or scalar `VFMA` stalls one cycle. Interleaving stores `VST2`/`VST4` see producer latency as tabulated and do not get the ordinary-vector-store adjustment. [Cortex-M85 r1p0 optimization guide, section 4.2, pages 43-44](https://support.arm.com/documentation/107950/0100)

## Align accesses to the datapath and manage base updates

For best 64-bit datapath bandwidth and latency, pair word/subword accesses that do not cross 32-bit boundaries, or use multiword accesses with a 32-bit-aligned start. Misaligned requests can add stalls. This is a byte/word boundary guideline, not a universal requirement to align every buffer to 16 bytes. [Cortex-M85 r1p0 optimization guide, section 4.4, page 44](https://support.arm.com/documentation/107950/0100)

The Cortex-M85 r1p0 optimization guide says scalar and MVE load/store bandwidth is well matched. Whether to use gather/scatter or multiple scalar transfers to and from vector registers depends on available scalar/vector address registers and whether the access can avoid bank conflicts. Gather/scatter base-register updates do not forward to following instructions and can add stalls. Immediate offsets may express addresses without depending on a just-updated base; alternatively, a separate vector add for the base update may fit between surrounding operations. These are conditional options: preserve address semantics, consider register pressure, and verify the resulting schedule. Nominal base-update latency rows do not remove the separate scatter/gather non-forwarding constraint. See [Helium instruction interleaving](techniques-helium-instruction-interleaving.md) and the [DTCM bank guidance](../hardware/hardware-dtcm-scatter-gather-banks.md) for related scheduling and banking considerations. [Cortex-M85 r1p0 optimization guide, section 4.10, page 49](https://support.arm.com/documentation/107950/0100)

All table values and issue properties here are scoped to the Cortex-M85 r1p0, Issue 01 guide. The Cortex-M85 r1p0 optimization guide reports processor timing and constraints, not measured workload speedups; inspect the emitted encoding and measure the actual target, especially when memory misses, bank conflicts, or misalignment affect the path. [Cortex-M85 r1p0 optimization guide, sections 2.2-3.1, pages 12-13; sections 3.6-3.15, pages 24-42; sections 4.2, 4.4, and 4.10, pages 43-44 and 49](https://support.arm.com/documentation/107950/0100)
