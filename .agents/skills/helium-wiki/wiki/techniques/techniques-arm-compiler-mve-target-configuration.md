---
id: techniques-arm-compiler-mve-target-configuration
title: Configure Arm Compiler targets for implemented Helium and floating-point features
type: techniques
architectures:
  - armv8.1-m
  - cortex-m55
  - cortex-m85
  - cortex-m52
tags:
  - compiler-target-configuration
  - helium
  - mve
  - floating-point
  - c
  - cpp
confidence: source-reported
reproducibility: snippet
related:
  - techniques-helium-auto-vectorization
  - techniques-armv81m-feature-initialization
  - patterns-cmsis-dsp-helium-migration
  - languages-c-float16-half-precision
sources:
  - https://support.arm.com/documentation/102095/0101/?lang=en
  - https://developer.arm.com/community/arm-community-blogs/b/architectures-and-processors-blog/posts/armv8_2d00_m-based-processor-software-development-hints-and-tips
  - https://github.com/Arm-Examples/Helium-Optimization/blob/bc9dfb67be64a2a5ee8e5325910a3f060a593863/General_optimizations/basic_optimization_topics.md#L49-L79
  - https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf
aliases:
  - __ARM_FEATURE_MVE bitmap
  - MVE mcpu march selection
  - Arm Compiler MVE target flags
  - armclang Cortex-M55 feature selection
  - armclang Cortex-M85 feature selection
  - __ARM_FEATURE_MVE
  - Arm Compiler Cortex-M52 feature selection
  - Cortex-M52 MVE target flags
prerequisites:
  - Know which Helium and floating-point features the target actually implements.
  - Keep compiler, linker, and disassembler CPU feature descriptions consistent.
---

Use these Arm Compiler 6 configuration examples when building for Cortex-M52, Cortex-M55, or Cortex-M85 with a particular combination of Helium (MVE) and floating-point features. Selecting features that the device does not implement can produce unsuitable code; architecture-only selection is also reported to miss processor-specific optimization.

The [Armv8.1-M software development hints and tips article](https://developer.arm.com/community/arm-community-blogs/b/architectures-and-processors-blog/posts/armv8_2d00_m-based-processor-software-development-hints-and-tips) recommends Arm Compiler 6.15 or later and says 6.14 supports Armv8.1-M but was less optimized. This is a historical recommendation from the Armv8.1-M software development hints and tips article, not a current minimum-version statement. With `-mcpu`, the Armv8.1-M software development hints and tips article says Cortex-M55/M85 default selections assume both Helium and FPU. Use the row matching the actual implementation:

| Processor | Configuration | `armclang -mcpu` | `armlink` / `fromelf --cpu` |
| --- | --- | --- | --- |
| Cortex-M55 | No Helium, no FPU | `-mcpu=cortex-m55+nomve+nofp` | `--cpu Cortex-M55.no_mve.no_fp` |
| Cortex-M55 | No Helium, with FPU | `-mcpu=cortex-m55+nomve` | `--cpu Cortex-M55.no_mve` |
| Cortex-M55 | Integer Helium, no FPU | `-mcpu=cortex-m55+nofp` | `--cpu Cortex-M55.no_fp` |
| Cortex-M55 | Integer Helium, scalar FPU | `-mcpu=cortex-m55+nomve.fp` | `--cpu Cortex-M55.no_mvefp` |
| Cortex-M55 | Full feature set | `-mcpu=cortex-m55` | `--cpu Cortex-M55` |
| Cortex-M85 | No Helium, no FPU | `-mcpu=cortex-m85+nomve+nofp` | `--cpu Cortex-M85.no_mve.no_fp` |
| Cortex-M85 | No Helium, with FPU | `-mcpu=cortex-m85+nomve` | `--cpu Cortex-M85.no_mve` |
| Cortex-M85 | Integer Helium, no FPU | `-mcpu=cortex-m85+nofp` | `--cpu Cortex-M85.no_fp` |
| Cortex-M85 | Full feature set | `-mcpu=cortex-m85` | `--cpu Cortex-M85` |

The [Armv8.1-M software development hints and tips article](https://developer.arm.com/community/arm-community-blogs/b/architectures-and-processors-blog/posts/armv8_2d00_m-based-processor-software-development-hints-and-tips) provides these architecture-level alternatives. They apply to Cortex-M55/M85, but using `-march` does not provide processor-specific optimizations:

| Configuration | `armclang -march` | `armlink` / `fromelf --cpu` |
| --- | --- | --- |
| No Helium, no FPU | `-march=armv8.1-m.main+dsp` | `--cpu 8.1-M.Main.no_mve.no_fp` |
| No Helium, with FPU | `-march=armv8.1-m.main+dsp+fp.dp` | `--cpu 8.1-M.Main.no_mve` |
| Integer Helium, no FPU | `-march=armv8.1-m.main+mve` | `--cpu 8.1-M.Main.no_fp` |
| Integer Helium, scalar FPU | `-march=armv8.1-m.main+mve+fp.dp` | `--cpu 8.1-M.Main.no_mvefp` |
| Full feature set | `-march=armv8.1-m.main+mve.fp+fp.dp` | `--cpu 8.1-M.Main` |

In these Arm Compiler options, `+nomve` suppresses Helium generation, and `+nofp` also implies `+nomve.fp`. The `+mve` architecture feature implies legacy `+dsp`. Keep the matching `--cpu` description on link and disassembly commands so tools interpret the feature set consistently. For example, a Helium-capable disassembly target can be selected with `fromelf -c --cpu=8.1-M.Main.mve.fp test.elf --output list.txt`. CDE disassembly additionally requires `-coprocN=cde` (where `N` is the coprocessor number); the [Armv8.1-M software development hints and tips article](https://developer.arm.com/community/arm-community-blogs/b/architectures-and-processors-blog/posts/armv8_2d00_m-based-processor-software-development-hints-and-tips) says CDE is unavailable on Cortex-M55/M85 revision 0.

Choose `-mcpu` when targeting a named processor and its microarchitectural optimizations; choose `-march` when deploying across compatible implementations, accepting architecture-level rather than processor-specific tuning. In both cases, select optional MVE and floating-point features and the ABI to match the fitted processor configuration; an architecture name alone does not establish implemented features or runtime access. [Coding for Helium v1.1, §6, p. 17](https://support.arm.com/documentation/102095/0101/?lang=en)

The Coding for Helium v1.1 guide's `-mcpu=cortex-m55` and `-march=armv8.1-m.main+mve.fp+fp.dp` commands are Arm Compiler examples. Use them only with the matching fitted feature configuration and ABI; a compiler target option is not a runtime hardware or permission check. [Coding for Helium v1.1, §§5–6, pp. 15, 17](https://support.arm.com/documentation/102095/0101/?lang=en)

ACLE C and C++ code can use `__ARM_FEATURE_MVE` to select an MVE-specific implementation. Its higher bit indicates MVE floating-point support, for example:

```c
#if (__ARM_FEATURE_MVE & 2)
  /* MVE floating-point implementation */
#endif
```

This is a compile-time feature test. It does not establish that runtime CP10/CP11 access has been enabled; startup configuration is covered by [techniques-armv81m-feature-initialization](techniques-armv81m-feature-initialization.md). The Arm Compiler flags and version advice here are source-specific and should not be treated as GCC/LLVM flags or a current toolchain recommendation.

## Match the target's implemented MVE features

Armv8.1-M and MVE are separate capability claims: an Armv8.1-M processor may omit MVE, implement integer MVE only, or implement integer and floating-point MVE. Scalar floating-point support, including optional double precision, does not imply vector double-precision support; MVE provides vector integer and supported vector floating-point formats, but no vector F64 arithmetic. Select compiler features from the fitted processor's actual implementation rather than assuming that every Armv8.1-M target has the same MVE feature set. [Arm Helium Technology: M-Profile Vector Extension (MVE) for Cortex-M Processors, Reference Book, PDF pp. 24, 36–37](https://github.com/arm-education/Arm-Helium-Technology/blob/main/HeliumTechnology_referencebook.pdf)

For ACLE code selection, `__ARM_FEATURE_MVE` is a compile-time 2-bit feature bitmap: bit 0 indicates integer MVE and bit 1 indicates MVE floating-point support. Its valid values are 0 (no MVE), 1 (integer MVE), and 3 (integer and floating-point MVE). Treat an absent macro as no MVE and test the appropriate bit when selecting an implementation. These checks select code at build time; they do not detect the feature set of an arbitrary processor at runtime. Keep the existing Cortex-M52, Cortex-M55, and Cortex-M85 toolchain selections above tied to each processor's supported combinations. [Coding for Helium v1.1, §7, pp. 21–22](https://support.arm.com/documentation/102095/0101/?lang=en)

An intrinsic-using source file must include `arm_mve.h` and be compiled for an MVE-capable target. These are the minimum requirements for explicit intrinsics to lower to MVE instructions; auto-vectorization is a separate transformation of ordinary C/C++ code and is not required for intrinsic calls. [Coding for Helium v1.1, §7, p. 22](https://support.arm.com/documentation/102095/0101/?lang=en)

## Cortex-M52 target feature selection

The [Helium optimization topics #1 - the basics article](https://github.com/Arm-Examples/Helium-Optimization/blob/bc9dfb67be64a2a5ee8e5325910a3f060a593863/General_optimizations/basic_optimization_topics.md)'s Arm Compiler 6.22-era table lists these Cortex-M52 configurations. Check the selected compiler release's documentation before adopting them.

| Configuration | Compiler CPU option | Arm linker/disassembler CPU option |
| --- | --- | --- |
| No Helium, no FPU | `-mcpu=cortex-m52+nomve+nofp` | `--cpu=cortex-m52.no_mve.no_fp` |
| No Helium, with FPU | `-mcpu=cortex-m52+nomve` | `--cpu=cortex-m52.no_mve` |
| Full listed feature set | `-mcpu=cortex-m52` | `--cpu=cortex-m52` |

Keep the compiler CPU selection aligned with the linker and disassembler CPU descriptions, and enable only features implemented by the fitted processor configuration. The [Helium optimization topics #1 - the basics article](https://github.com/Arm-Examples/Helium-Optimization/blob/bc9dfb67be64a2a5ee8e5325910a3f060a593863/General_optimizations/basic_optimization_topics.md) table groups armclang and GCC compiler spellings, while armlink and fromelf are Arm tools; do not transfer Arm linker flags to another toolchain or infer unlisted combinations. Runtime feature enablement remains a separate startup concern; see [Armv8.1-M feature initialization](techniques-armv81m-feature-initialization.md).
