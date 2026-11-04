---
description: RNNoise live microphone noise suppression with landscape LCD metrics.
category: audio
catalog: true
---

# RNNoise

The DevKit-E8 microphone supplies mono signed PCM at 48 kHz in 480-sample
(10 ms) blocks. The CMSIS Stream graph extracts the original 42 RNNoise
features, executes the recurrent INT8 ExecuTorch method with two Ethos-U85
delegates, applies the 22 band gains and pitch filter, and synthesizes enhanced
PCM with a 960-point inverse FFT and overlap-add. Enhanced audio is discarded
immediately by a bounded null sink, as selected for this demo.

![CMSIS Stream graph](generated/app.png)

The landscape LCD updates at up to 10 Hz with voice probability, input dBFS,
mean-mask suppression in dB, maximum inference time, current full-frame processing time, processed frame
count and capture gaps. Mask suppression is `-20 log10(mean band gain)` before
smoothing; it is an indicator of model gain, not measured noise reduction or
SNR. A true SNR would require a clean reference signal.

The CPU frontend and synthesis are reusable RNNoise nodes under
`common/nodes_impl/c/`; the model method, null-sink metrics, event protocol and
LCD drawing belong to this project. The graph colors the PTE node purple,
hardware drivers brown and CPU processing black. The model node contains
quantization, recurrent state and method invocation only. Feature spectrum and
pitch context travels on a separate graph branch to synthesis.

Measured complete-frame maximum: **7.825 ms**, including recovery from injected
capture interruptions, below the 10 ms hop deadline. The uninterrupted 3,005-frame
run had zero capture/LCD drops or deadline misses.

## Timing and buffering

Capture contributes one 10 ms block; overlap-add contributes one further 10 ms
hop. The hardware ring has eight slots (at most seven ready blocks, 70 ms) to
absorb scheduling jitter, rather than accumulating an utterance. Each enhanced
480-sample frame is discarded immediately after synthesis. A sequence gap resets frontend, network and synthesis histories
coherently when the next noncontiguous captured block is consumed; a hardware
PDM error restarts capture. Queued blocks remain bounded by the ring capacity. LCD snapshots are bounded (queue length three), published at 100 ms
intervals and drawn on the lower-priority event thread; drawing and VSYNC never
block the real-time audio thread. Dropped snapshots and deadlines are counted.

The project reserves two 64 KiB inference pools and a 256 KiB C++ heap for DSP
state and temporary FFT/vector storage. The original 96 KiB heap failed on the
first frontend FFT; live inspection identified `std::bad_alloc`. This was an
application reservation issue with sufficient remaining DTCM. Both project
linker scripts reserve the larger heap.

## Build and validate

Select `DevKit-E8@RNNoise_Release`, build and load through the CMSIS extension.
The startup test compares all five outputs against host fixtures and a
160-frame recurrent sequence before microphone capture begins. Inspect
`pipeline_error`, `stream_stats`, `rnnoise_test_passed`,
`rnnoise_parity_passed`, `rnnoise_numerical_passed` and
`rnnoise_benchmark_complete` with matching firmware symbols.

Generate from either repository root with
`python projects/rnnoise/app.py` or this directory with `python app.py`.
Run host DSP validation with `python tests/run_rnnoise_tests.py` from a compiler
developer environment (or select a C++17 compiler with `--compiler`). The test
checks the original 160-frame frontend fixture, Python synthesis, gain masking,
silence and exact reset equivalence. It repairs the original Python reference's
two-value silence return only in memory to satisfy its seven-value caller.

See [validation](VALIDATION.md), [model conversion](model/CONVERSION.md) and
[model benchmark](model/BENCHMARK.md). Source TFLite and imported PyTorch INT8
recurrences have documented numerical differences; the synthetic tests do not
establish speech quality. The original unquantized checkpoint is unavailable.

[Ethos issues and compiler-bug reproduction](ETHOS_ISSUES.md) records verified
toolchain problems, workarounds, versions and diagnostic artifacts for reporting.
The DSP [provenance and license notices](../../common/nodes_impl/c/rnnoise/NOTICE.md)
identify the two adapted Apache-2.0 source files; imported libraries are unchanged.
