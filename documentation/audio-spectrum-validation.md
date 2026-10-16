# Audio spectrum validation

The original thread-based validation below records the pre-Stream MRAM and
OSPI implementations. The current MRAM implementation and benchmark were
revalidated after migration; see [CMSIS Stream validation](cmsis-stream-validation.md).
The OSPI migration and its current benchmark are recorded separately in
[OSPI CMSIS Stream validation](ospi-stream-validation.md).

The source was adapted from `Spectrogram` in
`C:/Users/chrfav01/benchresults/testassistant/Blinky_M55_HP`, read directly from
the branch while leaving its `chess` checkout untouched.

## Processing and ownership

| Stage | Implementation | Owner |
| --- | --- | --- |
| Microphone, 48 kHz / 512 samples | common/nodes_impl/alif_e8/microphone.c | Audio thread and PDM ISR |
| Signed PCM normalization | common/nodes_impl/c/window.c | Audio thread |
| Signed 2x gain, NHWC [1,1,512,1] | project model/model.py and app_main.cpp | Audio thread / Ethos-U85 |
| Sliding window, 2048 samples / 512 hop | common/nodes_impl/c/sliding_buffer.c | Audio thread |
| DC removal | common/nodes_impl/c/window.c | Audio thread |
| Periodic Hann window | common/nodes_impl/c/window.c | Audio thread |
| RFFT then one-sided magnitude | common/nodes_impl/c/fft.c | Audio thread |
| Spectrum snapshot | project app_main.cpp, short mutex | Audio publishes; LCD copies |
| Palette, smoothing, peak decay, touch UI | project spectrum_ui.c | LCD thread |
| Power/pins, panel, VSYNC/cache, touch driver | common/nodes_impl/alif_e8/lcd.c | Init, then LCD thread |

No stage hides FFT, windowing or sliding-buffer work inside another stage.
The sliding buffer contains gained samples. A capture sequence gap discards
its history and waits for four new blocks before publishing another spectrum.
The FFT's packed DC/Nyquist entries get half the interior-bin amplitude scale.

The source model is a bias-free 1x1 convolution with a constant weight of 2,
with no activation. It processes a vector of signed samples, preserving sign.
The float input range [-1,1] is calibrated to int8; this introduces quantization
noise, including for very quiet microphone signals. The touch slider controls
display scale, not the network weight.

The common runtime initializer constructs one EmbeddedModule in static storage
and transfers the loader/allocator ownership to it. It deliberately lives until
reset. Projects own pool sizes/placement (64 KiB method + 64 KiB temporary),
input memory and thread stacks. The one audio thread uses `forward()` after a
startup test; the LCD never touches ExecuTorch.

## Host checks, October 15, 2026

- `tests/run_audio_tests.ps1 -PackRoot C:/ctools_pack`: PASS. Compiles the actual
  common C stages and project renderer with clang/CMSIS-DSP. Checks overlapping
  history and gap reset, silence, DC rejection, four tones across the spectrum,
  amplitude normalization, Nyquist, both display scales, clipping, slider/touch
  behavior, attack/release/peak decay, and framebuffer bounds. Also writes PPM
  previews. The log preview was visually inspected.
- `tests/test_project_scripts.py`: all 16 tests PASS, including regenerating the
  gain model, cache reuse, changed weights, and OSPI generation guards.
- Both quantizers calibrated four signed full-scale representative blocks and
  checked results against PyTorch with 0.04 absolute tolerance.
- Vela: 512 MACs, one NPU operator, zero CPU network operators, one Ethos
  delegate, 3072-byte PTE for each storage configuration. Float-boundary
  quantize/dequantize still runs on the CPU.
- CMSIS tools environment: generated format 1.0.0 validated against installed
  extension 1.72.0 schema. Toolbox 2.15.0, CMake 3.31.12, Ninja 1.13.2,
  Arm Compiler 6.24.0 and GCC 14.3.1 detected at the selected locations.

## MRAM Release hardware check, October 15, 2026

CMSIS build b-27 passed. CMSIS load-and-debug d-28 loaded the new image on
DevKit-E8 M55_HP. RTX listed Audio + Ethos at priority 48 (Realtime), LCD
animation at 40 (High), and Application at 24 (Normal). Audio was waiting for
thread flags between captures; LCD waited for delay/VSYNC between frames.

A live memory snapshot, using addresses resolved from this image's symbols:

| Counter | Observed value |
| --- | ---: |
| pipeline_error | 0 |
| gain_test_passed | 1 |
| audio_blocks | 13656 |
| spectrum_frames | 13641 |
| frames_presented | 2931 |
| audio_deadline_misses | 0 |
| lcd_deadline_misses | 0 |
| audio_ticks_max | 157648 |
| microphone_drops | 0 |
| microphone_overflows | 4 |

The debugger halted the running microphone repeatedly. PDM continued while the
core was stopped, and capture recovered from those counted overflows. This is
not a claim of zero overflows under debugger halts. The runtime checks prove
that inference and rendering advanced; they do not substitute for listening to
or physically interacting with the board.

SRAM0 allocation was 1,669,120 bytes out of 4 MiB, including 1,536,000 bytes
for the two RGB565 LCD buffers. The PTE remained in MRAM. The signed startup
ramp passed the numerical check and required nonzero Ethos PMU invocation and
cycle counts.

## OSPI validation

Release build b-29 passed with the 3072-byte gain model generated for OSPI.
The programming-preparation image reached `model_programming_ready=1`,
`app_stage=5`, `app_error=0`, and `app_service_error=0`. CMSIS flash programmed
3072 bytes successfully. Release build b-36 and load-and-debug d-37 ran the
new application with the following live snapshot:

| Counter | Observed value |
| --- | ---: |
| model_verified | 1 |
| model_crc32 | 0x64E926CF |
| pipeline_error | 0 |
| gain_test_passed | 1 |
| audio_blocks | 1044 |
| spectrum_frames | 1041 |
| frames_presented | 224 |
| audio_deadline_misses | 0 |
| lcd_deadline_misses | 0 |
| audio_ticks_max | 278073 |

RTX again reported audio priority 48 and LCD priority 40. Direct memory reads
showed nonzero normalized PCM, nonzero gained output and nonzero published FFT
magnitudes. PTE bytes remain at `0xC0000000`; the NPU/runtime writable pools and
LCD buffers occupy SRAM0. The direct OSPI CRC and successful signed-ramp NPU
check verify the external model path as well as application startup.

An earlier programming attempt with a one-second tool timeout left the old
external model in place. The application correctly rejected it with
`pipeline_error=-311` before creating audio or LCD threads. Repeating the
prepared programming sequence with a full completion wait resolved it.


## Gain-network benchmarks and final verification

Both `projects/template_{mram,ospi}/model/benchmark.yml` files have been replaced
with new measurements for the signed 512-sample gain network. Raw UART captures
are next to them. Each contains only the final launched run used for the
recorded statistics. Every warmup and sample
checks all outputs, requires one NPU invocation, verifies the PMU cycle/active
counter equality, and rejects counter overflow. The maximum absolute output
error was 0.008816, below the 0.04 tolerance, in both templates.

| Release measurement | MRAM | OSPI |
| --- | ---: | ---: |
| PTE bytes | 3072 | 3072 |
| NPU arena bytes | 1216 | 1216 |
| Compressed constants bytes | 192 | 192 |
| Method allocator peak bytes | 3752 | 3752 |
| Temporary allocator peak bytes | 1216 | 1216 |
| Median NPU cycles | 2079.5 | 9435 |
| Median NPU ms, 400 MHz | 0.00519875 | 0.0235875 |
| Median forward ms | 0.14859125 | 0.44615125 |

The forward timer surrounds only `module.forward()`: it excludes tensor setup,
output copy, validation and UART. Both timed regions include CPU float-boundary
quantize/dequantize and the existing PMU hooks. Audio capture and LCD rendering
start after measurement. These are network timings, not whole-pipeline timing.

Each YAML's `measurements.memory` records the model size, Ethos weights/arena
regions and observed ExecuTorch allocator peaks. The weights region is contained
in the model, and the arena is backed by the temporary pool; these overlapping
components must not be summed as independent allocations. Application pool
capacities, firmware allocations and linker placement are omitted. Vela
predictions are separate under `estimates.vela`. Each record also includes all
ten raw sample counts, clock rates, model/firmware SHA256 and
build/execution job IDs. Independent checks reproduced medians from the UART
samples and verified the recorded firmware hashes against the final Release
ELFs.

MRAM Debug build b-41 and Release b-42 passed with the benchmark code. OSPI
Release b-39 and Debug b-44 passed with the benchmark code. ProgramModel b-34 passed. Neither benchmark
change alters the external model bytes, which remain CRC 0x64E926CF for OSPI.

After the final MRAM benchmark launch d-43, a live snapshot showed
`pipeline_error=0`, `gain_test_passed=1`, `gain_benchmark_complete=1`,
`audio_blocks=10830`, `spectrum_frames=10827`, `frames_presented=2324`, and
zero audio/LCD deadline misses. The target was resumed and the serial port
released after inspection.

A later attempt to restore the final MRAM selection lost the debug connection.
The loader reported a CoreSight SWD/JTAG `FAULT ACK` and `No cores were
discovered`. A fresh session and the debugger reset fallback did not recover
the connection. After the user power-cycled the board, load-and-debug d-48
succeeded with the same measured MRAM Release firmware. Its startup gain and
benchmark checks passed again. A live snapshot showed `pipeline_error=0`,
`gain_test_passed=1`, `gain_benchmark_complete=1`, `audio_blocks=4470`,
`spectrum_frames=4467`, `frames_presented=959`, and zero audio/LCD deadline
misses. RTX confirmed audio priority 48 and LCD priority 40. The application
was resumed and the serial port released. Both measured Release firmware
hashes still match their benchmark records.
