# MRAM CMSIS Stream validation - October 15, 2026

This board-run record predates the sample-based node conversion. It describes
packet FIFOs, capture-gap recovery and invalid warm-up frames in that earlier
implementation. The current realtime graph uses scalar sample FIFOs, zero-padded
startup history and `EthosGain`; see the [current template](../projects/template_mram/README.md).
The live measurements below have not been repeated for that conversion.

The scalar conversion passed Release build **b-62** through `cmsis_action`.
The build-artefact symbol lookup found only `arm_rfft_fast_init_2048_f32`
(122 bytes) for the RFFT initializers in the Release image. The FFT wrapper
selects a size-specific initializer with `if constexpr`.

The default generated schedule has nine steps and 16,384 bytes of FIFO storage.
Three Python generator tests passed, including 128/256/512/1024-sample PCM
conversion rates. Production-wrapper host checks passed for 300-sample source
reblocking, mixed 256/512 rates, zero-padded overlap, the Ethos interface, all
eight FFT lengths from 32 to 4096, calibrated tone amplitude and LCD event
ownership/pool bounds. The existing audio DSP/renderer host checks also passed.
The CMSIS environment check passed with schema-validated environment format
1.0.0 from CMSIS Solution 1.72.0, Toolbox 2.15.0, CMake 3.31.12, Ninja 1.13.2,
and the selected AC6 6.24.0 compiler.

The MRAM template runs the generated `scheduler_app` dataflow graph and the
CMSIS Stream event queue on the DevKit-E8 M55_HP with the embedded MRAM model.
The OSPI template retains its existing scheduling.

## Generation and host checks

The local `cmsis-stream` Python package (3.2.0+git39.efdc521) was installed with
`uv pip install -e ./thirdparty/CMSIS-Stream/PythonPackage`. The local pack is
selected by the `path: thirdparty/CMSIS-Stream` solution entry and
`CMSIS:STREAM&Source` project component.

`uv run projects/template_mram/app.py` generates nine dataflow nodes and one
event-only LCD node, with a nine-step synchronous schedule. FIFO optimization
uses two 16-byte-aligned buffers totaling 16,416 bytes. Source/metadata hashes
were identical when generated from the repository root and project directory.
Graphviz produced `generated/app.pdf`. Simulated missing `dot` and nonzero
conversion exits both printed a warning and returned a completed graph.

`tests/run_stream_tests.ps1 -PackRoot C:/ctools_pack` passed using production
node wrappers and Stream FIFO/event types. It covers normalization, overlapping
history, four-block warm-up after a gap, window scale, three immutable queued
payloads, pool exhaustion, queue rejection/reuse and LCD event consumption.
`tests/run_audio_tests.ps1 -PackRoot C:/ctools_pack` also passed its original
silence, DC, tone, amplitude, Nyquist, display/touch and framebuffer-bound checks.
The generated scheduler passed a host C++ syntax check. Benchmark schema checks
and their 11 unit tests passed after replacing the MRAM capture and record.

## Release on the board

CMSIS build **b-52** passed; load-and-debug **d-53** loaded the resulting image.
The separate **DevKit-E8@Debug** build also passed (**b-54**).
The signed gain self-test checked all 512 outputs during one warm-up and ten
measured forwards. Maximum absolute error was **0.008816**, below **0.04**.
Each forward required one nonzero Ethos PMU invocation, equal cycle/active
counters and no PMU overflow.

The retained capture is `projects/template_mram/model/benchmark_uart.txt`.
The matching `benchmark.yml` records median NPU time **0.0050875 ms** and
median `forward()` time **0.151685 ms**, with CPU, AXI/NPU and timer at 400 MHz.
The model, arena, weights and method/temp allocator peaks remain 3072, 1216,
192, 3752 and 1216 bytes respectively. The record includes the Release ELF
SHA256 and the build/execution job IDs.

A live breakpoint in `LCD::processEvent` showed this call chain:

```text
LCD::processEvent
CMSISEventQueue::execute
event_thread_function
osThreadEntry
```

This proves the LCD is dispatched by Stream message passing. The generated
graph subscribes `SendToLCD` output 0 to LCD input 0. RTX reported the dataflow
thread waiting for microphone thread flags at priority 48, and the event thread
waiting for event flags at priority 40.

At that LCD breakpoint, before the current frame's `lcd_present()` completed:

| `stream_stats` field | Observed |
| --- | ---: |
| error | 0 |
| audio_blocks | 7406 |
| spectrum_frames | 7385 |
| frames_presented | 1589 |
| lcd_events_sent | 1590 |
| lcd_events_received | 1590 |
| lcd_events_dropped | 0 |
| audio_ticks_max | 152467 |
| audio_deadline_misses | 0 |
| lcd_deadline_misses | 0 |
| discontinuities | 6 |
| warmup_blocks | 21 |

The maximum observed processing time was approximately 0.3812 ms, below the
10.6667 ms block interval. The initial three invalid packets plus three per
capture discontinuity account for all 21 warm-up packets. Debugger halts leave
the microphone peripheral running; the graph resumed capture and discarded
partial history afterward. An earlier snapshot showed nonzero spectrum data
and `spectrum_peak_hz=93`.

The build-artefact tool reported 1,669,120 bytes in SRAM0, including both LCD
buffers and NPU-visible pools/input, out of 4 MiB. FIFO buffers and Stream runtime
storage occupy DTCM. The runtime uses the configured 32 KiB dataflow and 8 KiB
event stacks and a 64 KiB RTX dynamic pool.

These checks prove live capture/inference progress and successful LCD driver
presentation through the Stream event thread. Physical panel appearance and
touch interaction were not assessed during this migration; the existing
renderer and touch code are covered by the host checks.

## Final state

Release rebuild **b-57** passed with the same ELF SHA256 as **b-52**. After
switching back from Debug, the explicit Release cbuild-run file was programmed
with `flash` (509,952 bytes). The resulting UART self-test again passed, with
weights at the Release address `0x8026f8f0`. The retained benchmark above is the
recorded **d-53** measurement run. **g-59** started CMSIS Run on Release; the
target was left running, with no debug session and the owned serial port closed.
