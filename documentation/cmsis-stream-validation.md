# MRAM CMSIS Stream validation - October 15, 2026

This record covers the sample-based realtime graph with an int16 elementwise
Ethos gain on DevKit-E8 M55_HP. It supersedes the earlier packet-based MRAM
validation. The later OSPI migration is recorded in
[OSPI CMSIS Stream validation](ospi-stream-validation.md).

## Model and generation

`projects/template_mram/model/model.py` implements `x * 2.0` directly. The saved
quantized graph contains int16 activations and an int16 multiply constant, with
no convolution. `get_symmetric_a16w8_quantization_config(epsilon=2**-24)` keeps
its minimum scale below the approximately 1/32767 input scale. The exported
interface remains float; dividing signed int16 PCM by 32768 is exact. CPU
boundary quantize/dequantize operators surround the Ethos delegate.

Vela 5.1.0 compiled one NPU operator and zero CPU network operators, with zero
convolution MACs. The PTE is 2816 bytes. Its estimate is 128 cycles at 400 MHz,
with 1024 bytes of SRAM and a 16-byte constants region. The retained summary
was regenerated with intermediate artifacts enabled; after removing the added
`debug_artifact_path` compile specification, the deserialized runtime program
and compiled delegate bytes matched the firmware PTE exactly.

The local `cmsis-stream` Python package (3.2.0+git39.efdc521) and local pack at
`thirdparty/CMSIS-Stream` generate nine dataflow nodes and one event-only LCD
node. The nine-step synchronous schedule uses two 16-byte-aligned shared FIFO
buffers totaling 16,384 bytes. Ports carry primitive int16 PCM or float samples;
Python docstrings describe their meaning. Startup sliding history is zero-padded.
The signal-processing graph assumes continuous realtime input.

The microphone enables only PDM channel 4; channel 5 is disabled. Its 48 kHz,
signed 16-bit input is mono, with 512 samples per hardware capture. There is no
stereo interleaving before the mono FFT.

## Host and build checks

All 16 project-generation/cache tests passed, including int16 graph/tensor
checks, absence of convolution, calibration accuracy within 0.0001, cache reuse
and regeneration after changing the gain. The benchmark validator and its
11 unit tests also passed after updating the board capture and Vela summary.

The sample-based conversion previously passed three Python generator tests,
including 128/256/512/1024-sample PCM conversion rates. Production-wrapper host
checks passed for 300-sample source reblocking, mixed 256/512 rates, zero-padded
overlap, the Ethos interface, all eight FFT lengths from 32 to 4096, calibrated
tone amplitude and LCD event ownership/pool bounds. The existing audio
DSP/renderer host checks also passed.

Release build **b-63** passed through `cmsis_action`. The environment check
validated format 1.0.0 from CMSIS Solution 1.72.0, Toolbox 2.15.0, CMake 3.31.12,
Ninja 1.13.2 and AC6 6.24.0. FFT initialization selects the size-specific
initializer with `if constexpr`; the Release symbol lookup found only
`arm_rfft_fast_init_2048_f32` among the RFFT initializers.

## Release on the board

Load-and-debug **d-64** programmed Release and started the firmware. Its signed
ramp self-test checked all 512 outputs during one warmup and ten measured
forwards. The UART reported maximum absolute error **0.000030**, below the
**0.0001** tolerance. Every forward required one nonzero Ethos PMU invocation,
equal cycle/active counters and no PMU overflow.

The retained `projects/template_mram/model/benchmark_uart.txt` is the second
startup in d-64, after the debugger reset. `benchmark.yml` records its exact
cycle/timer samples, model/configuration/firmware hashes and job IDs. CPU,
AXI/NPU and timer clocks were all 400 MHz.

| Measurement | Result |
| --- | ---: |
| Median NPU cycles | 497 |
| Median NPU time | 0.0012425 ms |
| Median complete forward | 0.14742 ms |
| Model size | 2816 bytes |
| Ethos arena | 1024 bytes |
| Ethos constants region | 16 bytes |
| ExecuTorch method pool peak | 4776 bytes |
| ExecuTorch temporary pool peak | 1024 bytes |

Forward timing excludes tensor setup, output copying, validation, UART, capture
and drawing; it includes CPU float-boundary quantization and PMU hooks. The
previous int8 convolution reported 0.008816 maximum error, 0.0050875 ms median
NPU time and 0.151685 ms median forward time. Both quantization and operation
changed, so this comparison is for the complete models, not dtype alone.

Two live snapshots showed continued capture and display progress:

| `stream_stats` field | First | Second |
| --- | ---: | ---: |
| error | 0 | 0 |
| audio_blocks | 13222 | 16309 |
| spectrum_frames | 13222 | 16309 |
| frames_presented | 2837 | 3500 |
| lcd_events_sent | 2838 | 3500 |
| lcd_events_received | 2838 | 3500 |
| lcd_events_dropped | 0 | 0 |
| audio_ticks_max | 159448 | 159448 |
| audio_deadline_misses | 0 | 0 |
| lcd_deadline_misses | 0 | 0 |

The maximum processing time was **0.39862 ms**, below the **10.6667 ms** audio
block interval. `pipeline_error == 0`, `gain_test_passed == 1` and
`gain_benchmark_complete == 1`. A dataflow thread stack showed
`osThreadFlagsWait -> microphone_read -> read_microphone_samples ->
MicrophoneSource<short,512>::run -> scheduler_app -> stream_thread_function`
at priority 48. The event thread waited for event flags at priority 40.

The debugger initially supplied no active frame after a pause. Enumerating
threads and obtaining a call stack made global evaluation available. During
these inspection halts the microphone peripheral kept running: the first
snapshot had zero software drops and four hardware overflows. These counts
include debugger disruption and are not uninterrupted-run overrun measurements.
No gap detection or recovery packets are part of this realtime graph.

The build-artefact tool reported 1,669,136 bytes of SRAM0 load-segment usage,
including LCD buffers and inference pools, out of 4 MiB. The scalar FIFOs and
Stream runtime storage occupy DTCM. Dataflow/event stack reservations are
32 KiB/8 KiB, with a 64 KiB RTX dynamic pool.

These checks establish live capture, int16 Ethos inference, spectrum generation
and LCD driver presentation through Stream events. Physical panel appearance
and touch interaction were not assessed in this run; renderer and touch logic
are covered by the existing host checks. The validation breakpoint was removed
and the target was left running.

## Project-specific spectrum nodes - October 16, 2026

`LCD` and `SendToLCD` now live in `projects/template_mram/nodes/` and
`projects/template_mram/nodes_impl/`. Their spectrum event protocol, publication
cadence, retained spectrum/peak state and touch policy belong to this application.
Its drawing style and animation remain in the project's `spectrum_ui.c`.
`stream_types.h` also moved into the project because its counters describe this
pipeline. Common code retains reusable DSP and microphone nodes and the
UI-independent Alif panel presentation/touch driver. `EthosGain` remains local
to the MRAM project.

Generation through `app.py` from both the repository root and project directory
produced identical source/metadata hashes. All generated node includes resolved;
the schedule remained nine steps with 16,384 bytes of FIFO storage. All three
Python graph tests, production Stream-wrapper host checks and DSP/renderer host
checks passed. The generated CMSIS environment passed full schema validation:
Toolbox 2.15.0, CMake 3.31.12, Ninja 1.13.2 and AC6 6.24.0.

Release build **b-2** and load-and-debug **d-3** succeeded through `cmsis_action`,
using the matching `out/template_mram/DevKit-E8/Release/template_mram.axf`.
COM5 reported the signed 512-sample gain self-test and `Test_result: PASS`, with
maximum absolute error **0.000030**. Live inspection confirmed
`gain_test_passed == 1`, `gain_benchmark_complete == 1` and `pipeline_error == 0`.

| Diagnostic | First snapshot | Second snapshot |
| --- | ---: | ---: |
| `stream_stats.error` | 0 | 0 |
| `audio_blocks` / `spectrum_frames` | 1103 | 3530 |
| `frames_presented` | 237 | 758 |
| `lcd_events_sent` / `lcd_events_received` | 237 | 758 |
| `lcd_events_dropped` | 0 | 0 |
| `audio_deadline_misses` / `lcd_deadline_misses` | 0 | 0 |
| `audio_ticks_max` | 160649 | 160649 |
| `microphone_drops` | 0 | 0 |
| `microphone_overflows` | 0 | 2 |

Maximum audio processing time was **0.401623 ms** at the 400 MHz system timer,
below the 10.6667 ms hop interval. The audio thread waited in
`microphone_read -> MicrophoneSource::run -> scheduler_app` at priority 48;
the event thread waited for event flags at priority 40. The two microphone
hardware overflows were observed after inspection halts, during which the
peripheral continues running; this is not an uninterrupted-run overrun test.
Physical panel appearance and touch operation were not directly assessed.
No breakpoints remained, COM5 was closed, and the firmware was resumed and left
running.

## Sliding-buffer overlap storage - October 16, 2026

The Stream `SlidingBuffer` now builds each frame in its output FIFO and retains
only `outputSamples - inputSamples` samples for the next firing. It saves that
tail before downstream processing can overwrite the output. Initialization
zeroes the overlap, preserving startup padding; equal hop/frame lengths retain
no samples. The OSPI raw C API and its caller-owned full-frame history are
unchanged.

For the 2048-sample frame / 512-sample hop, the retained samples occupy
**6144 bytes** instead of 8192 bytes. Debugger `sizeof` measurements against the
matching old and new Release images showed the complete node shrinking from
**8224 to 6164 bytes**, a **2060-byte** reduction including the removed 12-byte
C history descriptor.

The production Stream-node executable passed with new tests for hop/frame pairs
1/8, 3/8, 4/8, 5/8, 8/8 and 1/1. These check every startup and subsequent frame,
reinitialization, overwritten downstream output and shared input/output storage.
Compile-time size checks reject full-frame storage and large zero-overlap nodes.
The existing 512/2048 pipeline and DSP/renderer host checks also passed, as did
the three Python graph tests. Generation from both working directories produced
identical source/metadata hashes, nine schedule steps and 16,384 FIFO bytes.

Release build **b-4** and load-and-debug **d-5** succeeded through `cmsis_action`
with matching `template_mram.Release+DevKit-E8` firmware and symbols. COM5
reported the signed gain self-test and `Test_result: PASS`, with maximum absolute
error **0.000030**. Live inspection confirmed `gain_test_passed == 1`,
`gain_benchmark_complete == 1` and `pipeline_error == 0`.

| Diagnostic | First snapshot | Second snapshot |
| --- | ---: | ---: |
| `stream_stats.error` | 0 | 0 |
| `audio_blocks` / `spectrum_frames` | 1403 | 4353 |
| `frames_presented` | 301 | 934 |
| `lcd_events_sent` / `lcd_events_received` | 301 | 935 |
| `lcd_events_dropped` | 0 | 0 |
| `audio_deadline_misses` / `lcd_deadline_misses` | 0 | 0 |
| `audio_ticks_max` | 159558 | 159558 |
| `microphone_drops` | 0 | 0 |
| `microphone_overflows` | 0 | 2 |

Maximum audio processing time was **0.398895 ms** at 400 MHz. The audio thread
waited for microphone input at priority 48; the LCD event thread ran at priority
40. The second snapshot caught one frame between event receipt and presentation.
The two microphone hardware overflows appeared across debugger inspection
halts; these snapshots do not measure uninterrupted operation. Physical panel
appearance and touch operation were not directly assessed. No breakpoints
remained, COM5 was released, and the updated firmware was left running.

## Fast RFFT packing check - October 16, 2026

Reviewed the [CMSIS-DSP floating-point real FFT specification](https://arm-software.github.io/CMSIS-DSP/latest/group__RealFFT.html)
against the existing implementation. `fft_forward` produces N packed floats:
DC at index 0, Nyquist at index 1, then interleaved complex interior bins.
`fft_magnitude` already treats DC/Nyquist separately and calls
`arm_cmplx_mag_f32` only on the N/2-1 pairs starting at index 2.

Added simultaneous negative DC, negative Nyquist and sine/cosine interior-bin
regression checks for all eight FFT lengths from 32 to 4096. They verify the
packed real/imaginary values, all calibrated output magnitudes and guards
around the N-float packed and N/2+1-float magnitude buffers. The Stream host
test suite passed through `tests/run_stream_tests.ps1`. Only comments,
documentation and host tests changed; the firmware algorithm is unchanged.
The existing board session remained running.

## RFFTMagnitude node name - October 16, 2026

Renamed the Python and C++ node classes and files to `RFFTMagnitude`, including
the package export, generated parameter type, scheduler, includes, graph diagram
and JSON metadata. The node's contract requires CMSIS-DSP fast F32 RFFT packing:
N floats with DC at index 0 and Nyquist at index 1, followed by complex interior
bins. It produces N/2+1 real magnitudes with the configured one-sided amplitude
scaling. General CFFT spectra require a general complex-magnitude node.

Both host suites and all three Python graph tests passed. Generation from the
repository root and project folder produced identical source/metadata hashes;
the schedule remained nine steps and FIFO storage 16,384 bytes. The exported
CMSIS environment passed full schema and toolchain checks. Release build **b-6**
and load-and-debug **d-7** succeeded through `cmsis_action`; build-artefact symbol
lookup confirmed `RFFTMagnitude<float,2048,float,1025>` in the Release ELF.

The matching live graph reported the same node type with `scale_ == 0.001953125`
(4/2048). COM5 recorded `Test_result: PASS` and maximum signed gain self-test
error 0.000030. `gain_test_passed == 1`, `gain_benchmark_complete == 1` and
`pipeline_error == 0` in both live snapshots.

| Diagnostic | First snapshot | Second snapshot |
| --- | ---: | ---: |
| `stream_stats.error` | 0 | 0 |
| `audio_blocks` / `spectrum_frames` | 1937 | 3648 |
| `frames_presented` | 416 | 783 |
| `lcd_events_sent` / `lcd_events_received` | 416 | 783 |
| `lcd_events_dropped` | 0 | 0 |
| `audio_deadline_misses` / `lcd_deadline_misses` | 0 | 0 |
| `audio_ticks_max` | 158250 | 158250 |
| `microphone_drops` | 0 | 0 |
| `microphone_overflows` | 0 | 2 |

Maximum processing time was 0.395625 ms at 400 MHz. The dataflow thread waited
for microphone input at priority 48, and the event thread waited for event flags
at priority 40. Two microphone hardware overflows appeared across inspection
halts; this is not an uninterrupted-run overrun measurement. Physical panel
appearance and touch operation were not directly assessed. No breakpoints
remained, COM5 was closed, and the firmware was resumed and left running.

## CMSIS-DSP audio preprocessing - October 16, 2026

Replaced the scalar loops in `common/nodes_impl/c/window.c` with
`arm_q15_to_float`, `arm_mean_f32`, `arm_offset_f32`, `arm_hanning_f32` and
`arm_mult_f32`. The periodic Hann definition and returned 2/sum(window)
amplitude scale are preserved. DC removal and window application remain usable
in place, and empty blocks remain no-ops. Q15-to-float conversion is exact over
the full signed 16-bit range; saturation is unnecessary in this direction.

Both existing host suites passed against CMSIS-DSP 1.18.0 sources. New audio
checks cover all 65,536 PCM values, periodic Hann coefficients and amplitude
normalization, DC removal and window application with separate and shared input
and output buffers, buffer guards, empty blocks, singleton blocks and lengths
with vector tails. The existing full spectrum/renderer checks and all three
Python graph tests also passed. The CMSIS environment schema and toolchain
checks passed.

MRAM Release build **b-8** and load-and-debug **d-9** succeeded through
`cmsis_action`; the artefact tools confirmed the updated Release ELF and all
five CMSIS-DSP functions used by the preprocessing backend.
COM5 recorded `Test_result: PASS` and maximum signed gain self-test error
0.000030. OSPI build and runtime verification are deferred at the user's request
until its rework.

`gain_test_passed == 1`, `gain_benchmark_complete == 1` and
`pipeline_error == 0` in both live snapshots.

| Diagnostic | First snapshot | Second snapshot |
| --- | ---: | ---: |
| `stream_stats.error` | 0 | 0 |
| `audio_blocks` / `spectrum_frames` | 1627 | 4907 |
| `frames_presented` | 349 | 1053 |
| `lcd_events_sent` / `lcd_events_received` | 349 | 1053 |
| `lcd_events_dropped` | 0 | 0 |
| `audio_deadline_misses` / `lcd_deadline_misses` | 0 | 0 |
| `audio_ticks_max` | 158772 | 158772 |
| `microphone_drops` | 0 | 0 |
| `microphone_overflows` | 0 | 2 |

Maximum audio processing time was 0.396930 ms at 400 MHz. The dataflow thread
waited for microphone input at priority 48; the LCD event thread waited at
priority 40. Two microphone hardware overflows appeared across debugger halts;
these snapshots do not measure uninterrupted operation. Physical panel
appearance and touch operation were not directly assessed. No breakpoints
remained, COM5 was closed, and the updated MRAM firmware was left running.

## Office DevKit-E8 startup failure - October 21, 2026

The initial spectrum UI appeared but capture, animation and touch handling did
not start. Live debugging found `app_result == pipeline_error == -401`, zero
NPU interrupts, and both gain self-test completion flags clear. Only the
application, idle and timer threads existed. The shared `ai_layer/` generation
record identified the object-detection YOLO model; the flashed firmware's
`model_pte_size` confirmed its 407,440-byte size. The gain template was loading
that model instead of its own network.

Regenerated the shared layer with
`uv run scripts/create_ai_layer.py --project template_mram`, selected
`DevKit-E8@Release`, then built and loaded only `template_mram` through
`cmsis_action` (build **b-9**, load-and-debug **d-10**). The environment passed
full schema validation: CMSIS-Toolbox 2.15.0, CMake 3.31.12, Ninja 1.13.2 and
AC6 6.24.0 were present. The corrected model is 2,816 bytes with one Ethos-U
delegate and CPU quantize/dequantize boundary operators. No firmware source
change was required. The existing SRAM1 address edit was retained.

On the office board, `gain_test_passed == gain_benchmark_complete == 1`,
`app_result == pipeline_error == stream_stats.error == 0`, and both Stream
threads were present. A live snapshot recorded 1,887 audio blocks/spectrum
frames, 405 presented frames, 405 LCD events sent/received, no dropped LCD
events and no audio/LCD deadline misses. NPU interrupts advanced from 1,898
to 4,349 between inspections. One microphone overflow was recorded across
debugger halts; this is not an uninterrupted capture measurement.

The user confirmed that LCD animation and dragging the scale slider both work.
The debugger also observed `spectrum_display_gain == 0.794840276` after the
slider interaction, from its initial value of 1.0. No breakpoints remained and
the firmware was resumed and left running. This establishes the cause of this
startup failure; it does not establish a hardware difference between boards.
