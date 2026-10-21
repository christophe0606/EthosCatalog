---
id: techniques-cortex-m55-epu-retention
title: Avoid EPU retention wake delays in performance-critical early Cortex-M55 workloads
type: techniques
architectures:
  - cortex-m55
tags:
  - extension-processing-unit
  - power-management
  - helium
  - floating-point
  - processor-initialization
  - c
confidence: source-reported
reproducibility: snippet
related:
  - techniques-armv81m-feature-initialization
  - patterns-cmsis-dsp-helium-migration
  - languages-c-float16-half-precision
sources:
  - https://developer.arm.com/community/arm-community-blogs/b/architectures-and-processors-blog/posts/armv8_2d00_m-based-processor-software-development-hints-and-tips
aliases:
  - Cortex-M55 EPU retention
  - CPDLPSTATE ELPSTATE
  - Helium wake latency
prerequisites:
  - Confirm the target is Cortex-M55 r0px or r1p0 and that its system power design uses automatic EPU retention.
  - Balance EPU availability against the application's power requirements.
---

On Cortex-M55 r0px and r1p0, a system may automatically place an enabled but idle Extension Processing Unit (EPU) into retention. The first subsequent floating-point or Helium instruction wakes it, and the transition can delay performance-sensitive work. This behavior depends on the system power design; the guidance does not apply broadly to other M55 revisions or Cortex-M85.

For performance-sensitive use, set `CPDLPSTATE.ELPSTATE` to `0b00` (ON) or `0b01` (clock gated). The [Armv8.1-M software development hints and tips article](https://developer.arm.com/community/arm-community-blogs/b/architectures-and-processors-blog/posts/armv8_2d00_m-based-processor-software-development-hints-and-tips)'s CMSIS-CORE example sets `0b01` while preserving the other register fields:

```c
PWRMODCTL->CPDLPSTATE = (PWRMODCTL->CPDLPSTATE & 0xFFFFFFCFUL) |
                        (0x1UL << PWRMODCTL_CPDLPSTATE_ELPSTATE_Pos);
```

The mask clears only the ELPSTATE field before writing the selected state. The [Armv8.1-M software development hints and tips article](https://developer.arm.com/community/arm-community-blogs/b/architectures-and-processors-blog/posts/armv8_2d00_m-based-processor-software-development-hints-and-tips) reports a reset value of `CPDLPSTATE = 0x00000333`, with ELPSTATE at `0b11` (OFF/retention). Restore ELPSTATE to `0b11` when the application no longer needs the EPU, for example before sleep, to allow the power-saving state again. Keeping the unit more available trades some power savings for reduced wake delay.

The [Armv8.1-M software development hints and tips article](https://developer.arm.com/community/arm-community-blogs/b/architectures-and-processors-blog/posts/armv8_2d00_m-based-processor-software-development-hints-and-tips) notes that the Cortex-M55 CMSIS-CORE v5.7 header lacks the `CPDLPSTATE` definition and that v5.8 adds it. Check the device header available to the project before using the named register and field symbols.
