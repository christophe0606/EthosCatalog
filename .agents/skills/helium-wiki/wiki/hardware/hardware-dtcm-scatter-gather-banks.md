---
id: hardware-dtcm-scatter-gather-banks
title: Place loads across Cortex-M55 and Cortex-M85 DTCM banks and M85 data-cache banks
type: hardware
architectures:
  - cortex-m55
  - cortex-m85
tags:
  - data-tcm
  - data-placement
  - helium
  - mve
  - cache
confidence: source-reported
reproducibility: concept
related:
  - hardware-itcm-instruction-data-contention
  - techniques-cortex-m85-latency-aware-scheduling
  - techniques-cortex-m85-memory-access-scheduling
  - patterns-cmsis-dsp-helium-migration
  - techniques-helium-instruction-interleaving
  - techniques-helium-fft-butterflies
  - techniques-armv8m-cache-management
  - techniques-cortex-m-memory-access-scheduling
sources:
  - https://developer.arm.com/community/arm-community-blogs/b/architectures-and-processors-blog/posts/armv8_2d00_m-based-processor-software-development-hints-and-tips
  - https://support.arm.com/documentation/102692/0101
  - https://support.arm.com/documentation/107950/0100
  - https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/TransformFunctions/arm_cfft_f32.c
  - https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/TransformFunctions/arm_cfft_f16.c
  - https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/PrivateInclude/arm_vec_fft.h
  - https://github.com/Arm-Examples/Helium-Optimization/blob/bc9dfb67be64a2a5ee8e5325910a3f060a593863/General_optimizations/processor_specific.md#L46-L57
aliases:
  - DTCM banks
  - scatter gather memory placement
  - DTCM bank conflicts
  - data TCM throughput
  - Cortex-M55 vector-load bank conflict
  - DTCM address bits 3:2
  - Cortex-M55 DTCM store-to-read bank condition
evidence_basis: "Bank-selection and processor timing details are source-reported in the Cortex-M55 r1p1 and Cortex-M85 r1p0 Processor Software Optimization Guides. CMSIS-DSP v1.18.0 links corroborate CFFT gather/scatter implementation structure."
---

Scatter/gather DSP code can benefit from DTCM when it issues separate data accesses that can reach different banks. On M55 and M85, the [Armv8.1-M software development hints and tips article](https://developer.arm.com/community/arm-community-blogs/b/architectures-and-processors-blog/posts/armv8_2d00_m-based-processor-software-development-hints-and-tips) describes four interleaved DTCM banks selected by address bits 2 and 3. Two separate accesses of 32 bits or less can be carried out simultaneously when they target different banks.

Arrange the accessed addresses so the concurrent accesses differ in bits [3:2]; accesses mapping to the same bank cannot use the stated overlap. This is conditional on access size and bank selection. The [Armv8.1-M software development hints and tips article](https://developer.arm.com/community/arm-community-blogs/b/architectures-and-processors-blog/posts/armv8_2d00_m-based-processor-software-development-hints-and-tips) contrasts this with AXI data accesses, where the D-cache lookup handles one address per cycle, but it does not claim a general twofold throughput gain for every DTCM workload or vector load. Read that article comparison separately from the Cortex-M85 r1p0 optimization guide's LSU and L1 details below: the Cortex-M85 r1p0 optimization guide describes same-cycle loads and their bank conflicts for that processor, not a general DTCM-versus-AXI throughput result. [Arm Cortex-M85 Processor Software Optimization Guide, r1p0, Issue 01 (26 April 2023), §4.3, p. 44](https://support.arm.com/documentation/107950/0100)

## Cortex-M85 r1p0 DTCM and L1 data-cache banks

The Cortex-M85 r1p0 optimization guide says DTCM and the L1 data cache each have four 32-bit banks, selected by address bits `[3:2]`. Two load instructions issued in the same cycle to the same bank conflict: the second access stalls for one cycle while the first proceeds. The Cortex-M85 r1p0 optimization guide specifically recommends avoiding adjacent scalar loads and MVE gather loads that access the same bank where the code and access pattern allow it. [Arm Cortex-M85 Processor Software Optimization Guide, r1p0, Issue 01 (26 April 2023), §4.3, p. 44](https://support.arm.com/documentation/107950/0100)

For an address-bit illustration, addresses separated by 16 bytes have the same `[3:2]` bits and select the same bank; consecutive aligned 32-bit words cycle through the four `[3:2]` values. This illustrates bank selection only. Different-bank accesses do not guarantee that all other pipeline, address, and memory conditions permit overlap.

The Cortex-M85 r1p0 optimization guide distinguishes stores from this same-cycle load rule: buffering of store data decouples pipeline store-instruction flow from bank conflicts. It also says DTCM bank conflicts can occur between CPU and DMA accesses. The four banks can serve CPU and DMA, each consuming at most two banks on a sustained basis, with some conflict-related loss expected; DMA access is not free. [Cortex-M85 r1p0 optimization guide, §4.3, p. 44](https://support.arm.com/documentation/107950/0100)

When comparing MVE scatter/gather with scalar accesses into vector registers, consider the available scalar and vector registers for holding addresses and how easily the access pattern can avoid bank conflicts. This is an implementation trade-off whose suitability depends on register pressure and the access pattern. [Cortex-M85 r1p0 optimization guide, §4.10, p. 49](https://support.arm.com/documentation/107950/0100)

## Cortex-M55 r1p1 vector-load conflict timing

The Cortex-M55 r1p1 optimization guide states that its TCM has four banks, each 32 bits wide. For addresses requested by one vector load, equal address bits [3:2] select the same bank; those addresses cannot be loaded in the same cycle, and the Cortex-M55 r1p1 optimization guide reports a one-cycle bank-conflict penalty. This timing statement is specific to Cortex-M55 r1p1 and should not be applied as an M85 timing claim. [Arm Cortex-M55 Processor Software Optimization Guide, r1p1, Issue 03, §4.1.3, p. 47](https://support.arm.com/documentation/102692/0101)

As an original illustration of the stated bit rule, `0x20010000` and `0x20010010` both have bits [3:2] equal to `00`, so requests for both in one vector load map to the same bank. An illustrative address such as `0x20010004` has bits [3:2] equal to `01` and selects a different bank. These are address-bit examples, not measured layouts or a guarantee that every different-bank access can proceed simultaneously under all other resource and data conditions. [Cortex-M55 r1p1 optimization guide, §4.1.3, p. 47](https://support.arm.com/documentation/102692/0101)

When the application's vector-load access pattern permits it, consider a data layout or stride that places concurrently requested words in different banks while preserving the program's access semantics. Verify the actual addresses and workload: the [Cortex-M55 r1p1 optimization guide](https://support.arm.com/documentation/102692/0101) establishes the same-bank conflict condition and penalty, not a general throughput gain from changing layout. See [techniques-cortex-m-memory-access-scheduling](../techniques/techniques-cortex-m-memory-access-scheduling.md) for related memory-access timing guidance.

## CMSIS-DSP CFFT gather/scatter implementation

The CMSIS-DSP v1.18.0 [F32 CFFT](https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/TransformFunctions/arm_cfft_f32.c) and [F16 CFFT](https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/TransformFunctions/arm_cfft_f16.c) sources show the released final-stage kernels loading irregular lane positions with gathers, forming sums and differences without loading twiddles, and scattering the outputs. The private [FFT helper header](https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/PrivateInclude/arm_vec_fft.h) also contains MVE gather/scatter helpers. Check the actual gather offsets and buffer addresses against the target's bank-selection rule before changing data placement. See [Helium FFT butterflies](../techniques/techniques-helium-fft-butterflies.md) for the released implementation references.

## Keep Cortex-M55 store-to-read and load-pair hazards separate

The [Processor specific details article](https://github.com/Arm-Examples/Helium-Optimization/blob/bc9dfb67be64a2a5ee8e5325910a3f060a593863/General_optimizations/processor_specific.md) also reports a DTCM condition on Cortex-M55: a read that follows a write to the same DTCM bank may be delayed. When a store and following load access independent buffers, evaluate offsets or placement that put those accesses in different banks, then measure the complete workload on the target. A load that must read the value just stored at the same address retains that dependency; changing bank placement cannot remove it. This store-to-read case is distinct from the same-bank conflict conditions for paired or gather loads described above. Preserve the existing Cortex-M55 r1p1 and Cortex-M85 r1p0 qualifications; do not generalize a core-specific rule to another revision.
