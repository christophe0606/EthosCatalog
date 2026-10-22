<!-- Copyright 2026 Arm Limited and/or its affiliates. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Handwriting synthesis port assessment

Status: experimental port authorized on 2026-10-22 with an explicit user
exception for the original work's missing license. Conversion and Ethos-U85
compilation, Release hardware benchmarking and the animated UART/LCD demo
succeeded. Partial-region display optimization and final review passed;
the final Release firmware is running.

## Requested application

Target the Alif E8 DevKit Cortex-M55 HP and Ethos-U85. Accept a sentence over
UART, start synthesis when Return is pressed, erase the previous sentence,
and animate white pen strokes on the black landscape LCD. Text that exceeds
the horizontal space continues on successive lines up to the available
screen height. The intended model
flow is pretrained weights to PyTorch `.pt2`/`.safetensors`, followed by
ExecuTorch and Vela, with post-training quantization only.

## Licensing status and user exception

The repository's mandatory reuse rule requires an Apache-compatible license
and preservation of copyright and license notices. No Apache-compatible reuse
permission was found for this upstream implementation or its supplied
pretrained checkpoint. This is a missing-permission finding; no GPL license
was identified for the network itself. After this finding was reported, the
user explicitly instructed us to continue, document the missing license,
retain the original reference, and preserve author attribution and copyright.
This is a project-rule exception, not an upstream license grant. Original
material and weights must not be described as covered by Apache-2.0.

The checked upstream revision is
`5f58984c3bc793bd8930bad7012cd2008d4a4a66`:

- The [repository tree](https://github.com/sjvasquez/handwriting-synthesis/tree/5f58984c3bc793bd8930bad7012cd2008d4a4a66)
  has no license file, and its README does not state a reuse license.
- [Upstream issue #16](https://github.com/sjvasquez/handwriting-synthesis/issues/16)
  requests a license. The requester's later assumption of MIT licensing is
  explicitly withdrawn; no grant from the model author appears there.
- The [checkpoint directory](https://github.com/sjvasquez/handwriting-synthesis/tree/5f58984c3bc793bd8930bad7012cd2008d4a4a66/checkpoints)
  contains pretrained TensorFlow checkpoint files. Availability does not
  establish the reuse permission required by this project.

Dependency and dataset rights would still need a complete audit if permission
for the source and weights is obtained. A fork's license alone is insufficient
evidence that the original checkpoint has compatible terms.

## Work completed

The planner's analysis agent performed a read-only source, licensing and
architecture assessment. Workflow evidence is retained in
`.flow/handwriting-synthesis/ANALYSIS.md` and the resumable state in
`.flow/handwriting-synthesis/CHECKPOINT.md`. The `.flow` directory is ignored
by Git; this document preserves the decision in the tracked project files.

At the analysis checkpoint, no upstream code or weights had been imported and
no board session had been modified. The CMSIS environment check passed with
Toolbox 2.15.0, CMake 3.31.12, Ninja 1.13.2, AC6 6.24.0 and GCC 14.3.1. The
generated environment passed the installed extension's full JSON schema check.
See the workflow checkpoint for the latest conversion and board state.

The converted model has about 3.63 million parameters. Its actual compiled PTE
is 3,355,648 bytes (3.20 MiB), larger than the default 2 MiB M55 HP
executable/model MRAM region, so the application was
created from `projects/template_ospi` with separate Handwriting Debug,
Release and ProgramModel target sets. Two RGB565 framebuffers require
1,536,000 bytes of SRAM0. Framebuffer memory is a final-demo planning figure;
it is not allocated in the benchmark-only firmware.

The converter reconstructed 3,632,431 inference parameters. Over 300 forced
steps, a separate NumPy implementation using the original checkpoint agrees
with PyTorch to a maximum GMM output absolute error of approximately 2.48e-5.
This is a source-equation comparison; the original TensorFlow runtime was
not executed.

Full-range a16w8 post-training quantization reports approximately 0.18–0.44%
normalized RMSE across the five dense methods on held-out generated traces.
Host previews remain readable. These tests do not establish recognition
accuracy on a labeled handwriting dataset.

The generated project-local PTE is 3,355,648 bytes with five genuine
Ethos-U85 delegates and CPU quantize/dequantize boundaries. Recurrent
orchestration, attention and mixture sampling remain on the CPU. The Release
benchmark measured a median 117.017505 ms per complete evolving stroke over
64 steps after warmup, with a 112.8710975 ms NPU subtotal. This is about 8.55
generated points per second and excludes UART printing and display work.
All five dense fixtures matched within one quantized output LSB; CPU
fixtures passed and hardware PMU activity was verified without overflow.
See the project-owned benchmark evidence, model conversion report and
`model/ADAPTER_CONTRACT.md` for exact shapes, state order and tolerances.

The complete UART/LCD Release build b-41 fits the current layout: 515,984 bytes of
MRAM and 1,732,624 bytes of SRAM0, including both LCD buffers and reserved
ExecuTorch pools. DTCM read/write allocation is 164,724 bytes, with heap and
stack reservations also fitting. Host checks cover input editing, submission,
replacement, bounds, landscape pixels and wrapping a real generated trajectory
over three lines. Final hardware demo acceptance is in progress.

The first running demo passed startup checks and produced visible handwriting;
the user confirmed the physical display. The user then found the 25 MHz
animation too slow and requested a higher OSPI clock for this application.
Release b-35 verified the board's guarded 100 MHz read-clock API (400 MHz AXI,
divider 4), full model CRC and all dense fixtures. Its 64-step median is
33.521788 ms for the complete stroke and 30.309374 ms for the NPU subtotal,
a 3.49x synthesis speedup. The 117 ms result above remains the preserved
25 MHz baseline. The 75-character hardware example completed across three
lines without ink overflow or pipeline errors. Overlapping the safe framebuffer
release wait with the next inference improved generated-point processing throughput from about
14 to 24.95 points/s: 1,739 points in 69.698 seconds. This recorded build still
copied and cache-cleaned the full framebuffer for each point. The user's next
request is to update only the changed pixels. The partial-region implementation
preserves continuous LCD scanout and buffer ownership while reducing CPU copy
and cache work. Release b-39 processed the identical 1,739 generated trajectory
points for the seed-42 sentence in 61.013 seconds (28.50 generated points/s),
a further 14.23% throughput gain.
Only 813,300 bytes were copied and 2,309,152 bytes cache-cleaned, including the
sentence clear, versus approximately 1.34 GB each with full-frame processing.
All generated results were processed across three lines, including pen-up results
that change no pixels; this is not an LCD frame or inked-point count. There were
1,678 frame submissions. No pipeline, LCD, input-drop or
ink-overflow errors. Full-frame-reference host checks also passed for every
pixel of a separate 1,952-point trajectory.

## Final validation

Review confirmed memory, LCD buffer ownership/cache bounds, catalog and hardware
evidence. Its coordinate-outlier, source-notice and measurement-wording findings
have been corrected. Fast-math host regressions passed and preserved identical
ordinary pixels. Final Release b-41 repeated the same 75-character run and UART
acceptance with zero errors/drops, and is running with the serial port released.
The focused review passed with all findings resolved. The final firmware is
left running with breakpoints cleared and COM7 available for the user's terminal.
