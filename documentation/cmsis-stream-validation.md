# MRAM CMSIS Stream validation - October 15, 2026

This record covers the sample-based realtime graph with an int16 elementwise
Ethos gain on DevKit-E8 M55_HP. It supersedes the earlier packet-based MRAM
validation. The OSPI template retains its existing implementation.

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
