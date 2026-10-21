---
id: languages-c-float16-half-precision
title: Use _Float16 for half-precision arithmetic and Helium vector data
type: languages
architectures:
  - armv8.1-m
  - cortex-m55
  - cortex-m85
  - cortex-m52
tags:
  - half-precision
  - floating-point
  - helium
  - mve
  - c
  - cpp
confidence: source-reported
reproducibility: snippet
related:
  - techniques-arm-compiler-mve-target-configuration
  - techniques-helium-auto-vectorization
  - patterns-cmsis-dsp-helium-migration
  - techniques-cortex-m55-epu-retention
  - techniques-helium-fft-butterflies
  - techniques-helium-df1-iir-vectorization
  - patterns-cmsis-nn-mve-runtime-selection
sources:
  - https://developer.arm.com/community/arm-community-blogs/b/architectures-and-processors-blog/posts/armv8_2d00_m-based-processor-software-development-hints-and-tips
  - https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/TransformFunctions/arm_cfft_f16.c
  - https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/FilteringFunctions/arm_biquad_cascade_df1_init_f16.c
  - https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/FilteringFunctions/arm_biquad_cascade_df1_f16.c
  - https://github.com/Arm-Examples/Helium-Optimization/blob/bc9dfb67be64a2a5ee8e5325910a3f060a593863/General_optimizations/basic_optimization_topics.md#L80-L107
  - https://github.com/Arm-Examples/Helium-Optimization/blob/bc9dfb67be64a2a5ee8e5325910a3f060a593863/Memory/Software_optimization_for_memory_constrained_devices.md#L105-L121
  - https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf
aliases:
  - half precision floating point
  - IEEE binary16
  - _Float16 arithmetic
  - fp16 Helium
  - C half precision
  - F16 storage footprint
  - half-precision working-set size
evidence_basis: "Language and processor capability details are source-reported by the public Arm article Getting started with Armv8.1-M based processor: software development hints and tips. CMSIS-DSP v1.18.0 code links corroborate half-precision implementation examples."
---

Use IEEE binary16 (`_Float16`) for floating-point data or arithmetic on Armv8.1-M when its reduced precision and dynamic range fit the application. It halves storage per value versus single precision and allows twice as many Helium lane operations per vector, but lane count alone does not predict whole-application performance; conversion, memory behavior, vectorization, and the workload still matter.

The [Armv8.1-M software development hints and tips article](https://developer.arm.com/community/arm-community-blogs/b/architectures-and-processors-blog/posts/armv8_2d00_m-based-processor-software-development-hints-and-tips) describes scalar half-precision arithmetic on M55/M85 when an FPU is implemented, and Helium half-precision vector operations when the floating-point vector feature is present. `_Float16` is an arithmetic type. ACLE `__fp16` is intended for storage and conversion, with arithmetic promoted to a wider type, so use `_Float16` when half-precision arithmetic is intended.

The [Armv8.1-M software development hints and tips article](https://developer.arm.com/community/arm-community-blogs/b/architectures-and-processors-blog/posts/armv8_2d00_m-based-processor-software-development-hints-and-tips) associates `_Float16` with the C11 extension ISO/IEC TS 18661-3:2015 and recommends selecting C11 for C projects with the `-std=c11` compiler option. It reports that Arm Compiler 6, LLVM, and GCC of the time also accepted the type in other language modes; compiler and C++ support remain toolchain-version dependent. Where supported, an `f16` suffix and explicit cast express binary16 constants and conversions:

```c
_Float16 x = 0.5f16;
_Float16 y = (_Float16)0.25;
_Float16 z = x * y;
printf("%f\n", (double)z);
```

The `printf` conversion promotes the value to `double` for `%f`. Do not confuse IEEE binary16 with bfloat16 (`__bf16`), which is a different 16-bit format; the [Armv8.1-M software development hints and tips article](https://developer.arm.com/community/arm-community-blogs/b/architectures-and-processors-blog/posts/armv8_2d00_m-based-processor-software-development-hints-and-tips) states that Armv8.1-M does not support bfloat16.

## Half-precision DSP implementation examples

The linked [CMSIS-DSP F16 CFFT](https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/TransformFunctions/arm_cfft_f16.c) and [F16 DF1 implementation](https://github.com/ARM-software/CMSIS-DSP/blob/v1.18.0/Source/FilteringFunctions/arm_biquad_cascade_df1_f16.c) show release-specific lane layouts and coefficient handling. See [Helium FFT butterflies](../techniques/techniques-helium-fft-butterflies.md) and [Helium DF1 IIR vectorization](../techniques/techniques-helium-df1-iir-vectorization.md) for those implementation patterns and numerical considerations.

Choose half precision using the application's error tolerance and data range. Q15's 16-bit integer lanes do not establish numerical equivalence to `_Float16`, and lane count alone does not predict whole-kernel throughput.

## Data width and working-set choice

When the processor configuration and compiler support the required F16 operations, `_Float16` can reduce array storage compared with F32 and can place more values in a vector register. Select the narrowest representation that meets the application's range, precision, rounding, and saturation requirements; a smaller element does not establish numerical equivalence or guarantee a faster complete kernel. MVE does not provide vector double-precision arithmetic, so double expressions remain scalar work even when surrounding operations use MVE.

When comparing candidate widths, include the full range of intermediate and accumulator values, not just the stored inputs and outputs. Check whether conversions or extra headroom are needed to preserve the kernel's precision, rounding, and saturation contract, and judge the effect across the complete kernel instead of inferring it from element width or lane count. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 32, 34, 36–37](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

For fixed-point kernels, account for the product width and every intermediate before selecting the stored element width; see [Helium fixed-point widths, rounding and saturation](../techniques/techniques-helium-fixed-point-arithmetic.md) for Q-format scaling, widening, and narrowing contracts.
