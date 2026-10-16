# OSPI CMSIS Stream validation - October 16, 2026

`template_ospi` now uses the same scalar CMSIS Stream graph and application
behavior as `template_mram`. Its `EthosGain`, `SendToLCD`, `LCD`, ExecuTorch gain
adapter, renderer, diagnostics and scheduler integration are separate files in
the OSPI project. It imports no MRAM application nodes. Common processing nodes,
board drivers and Stream runtime configuration remain shared.

## Generation and host checks

Graph generation passed from the repository root and the OSPI project folder.
The generated text sources and metadata were identical from both locations.
The nine-step schedule uses 16,384 bytes in two aligned shared FIFO buffers.
The generated graph and node copies match MRAM, apart from the project name.
`app_main.cpp`, renderer, application types, hardware interface, Stream startup,
model source and self-test constants also match MRAM. The OSPI hardware adapter
adds model CRC verification before constructing the runtime.

The following checks passed:

- Seven Python graph checks, including rate balancing for both templates and
  distinct OSPI ownership of the application node classes.
- Stream C++ node and C audio/renderer host suites against both templates,
  using CMSIS-DSP 1.18.0 and production node wrappers.
- Sixteen project/model script checks and eleven benchmark schema/provenance
  checks. Both benchmark YAML records pass the standalone validator.
- `git diff --check`.

The generated `.cmsis/tools-environment.yml` passed full validation against
the installed CMSIS Solution 1.72.0 schema, format 1.0.0. Its solution resolves
to this repository's `EthosCatalog.csolution.yml`; all selected paths exist.
The exported PATH and variables were imported only into the checking process.
The selected executable was
`C:/Users/chrfav01/.vscode/extensions/arm.cmsis-csolution-1.72.0-win32-x64/tools/cmsis-toolbox/bin/cbuild.exe`.
It reported CMSIS-Toolbox 2.15.0, CMake 3.31.12 and Ninja 1.13.2. Detected
toolchains were AC6 6.22.0/6.24.0, Clang 18.1.3/22.1.0 and GCC 13.3.1/14.3.1.
The firmware builds used the selected AC6 6.24.0.

## OSPI storage and builds

The project-owned model and quantizer now match MRAM's signed elementwise
`x * 2.0`: int16 activations and multiply constant, no convolution, float
boundaries, and a 0.0001 self-test tolerance. Vela 5.1.0 compiled it for
`Ethos_U85_SRAM_OSPI` / `Shared_Sram`, with one NPU multiply and CPU boundary
quantize/dequantize operators. The PTE is 2,816 bytes with CRC32 `0xCBF6A854`.
Its SHA256 is
`3674e8befb4ff5e71bfcfd6c7daf3475c7acd8b0169c5c1652d207176d2a2df3`.
The saved Vela summary was regenerated from the same project cache and OSPI
configuration; its 300 command bytes and 16 constant bytes match the deployed
PTE. Compiler estimates remain separate from measured timings.

CMSIS actions successfully built OSPI Debug (**b-13**), ProgramModel (**b-16**)
and the final OSPI Release (**b-18**). ProgramModel excludes the Stream runtime,
generated scheduler and AI layer. With its matching symbols (**d-17**), the
board stopped in `model_programming_wait` and reported readiness 1, stage 5,
application error 0 and service error 0. After detach, the `flash` tool confirmed
successful programming of all 2,816 bytes with the source-built IS25WX loader.

Application code remains in MRAM; the model stays at `0xC0000000` in OSPI1.
The existing XIP setup, CPU non-cacheable OSPI mapping, U85 memory attributes,
SRAM0 inference/framebuffer placement and programming handoff are retained.
A stale external model was rejected with error -311 before reprogramming.

## Matching Release firmware on DevKit-E8 M55_HP

Load-and-debug **d-19** succeeded with
`out/template_ospi/DevKit-E8/Release/template_ospi.axf` selected in both the
run description and debugger configuration. Build-artefact tools confirmed the
Release context, generated `scheduler_app`, `EthosGain<float,512,float,512>`
and `RFFTMagnitude<float,2048,float,1025>` in that ELF. The live magnitude node
has scale `0.001953125` (4/2048).

UART4 on COM5 at 115200 8N1 recorded `Test_result: PASS` and maximum absolute
gain error 0.000030. One warmup and ten measured forwards checked all 512 signed
samples and required one NPU invocation, matching cycle/active counters and no
PMU overflow. The final startup capture and benchmark are saved in the OSPI
project's [model folder](../projects/template_ospi/model/benchmark.yml).
Median NPU time was 0.010465 ms; median forward time was 0.4172775 ms at
400 MHz. The arena is 1,024 bytes, constants 16 bytes, and method/temp pool
peaks 4,776/1,024 bytes. The firmware SHA256 is
`bf69564d0d42c196b3d157c44214c9564e339f2d9150f8ace526d8d2a71cf538`.

Both live snapshots had `model_verified == 1`, CRC32 `0xCBF6A854`,
`gain_test_passed == 1`, `gain_benchmark_complete == 1` and `pipeline_error == 0`.

| Diagnostic | First snapshot | Second snapshot |
| --- | ---: | ---: |
| `stream_stats.error` | 0 | 0 |
| `audio_blocks` / `spectrum_frames` | 2698 | 5183 |
| `frames_presented` | 579 | 1112 |
| `lcd_events_sent` / `lcd_events_received` | 579 | 1113 |
| `lcd_events_dropped` | 0 | 0 |
| `audio_deadline_misses` / `lcd_deadline_misses` | 0 | 0 |
| `audio_ticks_max` | 272558 | 272558 |
| `microphone_drops` | 0 | 0 |
| `microphone_overflows` | 0 | 2 |

Maximum audio processing time was 0.681395 ms, below the 10.667 ms block
deadline. The second snapshot caught one LCD event before presentation
completed. The live LCD node retained a spectrum and reported a 46 Hz peak.
The dataflow thread waited for microphone input at priority 48; the event
thread waited for event flags at priority 40. Two microphone hardware overflows
appeared across debugger halts, so the second snapshot is not an uninterrupted
overrun measurement. Physical panel appearance and touch were not directly
assessed. The renderer and event/touch callbacks passed the host checks.

No breakpoints remain, COM5 is closed, OSPI Release is the active target, and
the firmware was resumed and left running.
