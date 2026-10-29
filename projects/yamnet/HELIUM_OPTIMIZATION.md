# YAMNet CPU convolution optimization

Release **b-100**, matching-ELF session **d-101**, reduces median complete
`module.forward` time from **1482.160096 ms to 167.678814 ms (8.839x)**.
The bottleneck was the CPU fallback, rather than NPU execution. The PTE,
quantization and 14 Ethos delegates are unchanged. All model bytes remain
directly in OSPI at 100 MHz; no full-model SRAM copy was introduced.

| Measured scope, 400 MHz | Portable CPU baseline | Helium version |
|---|---:|---:|
| Complete forward, median of ten warmed calls | 1482.160096 ms | 167.678814 ms |
| NPU PMU subtotal, median | 60.735478 ms | 60.736304 ms |
| Complete forward minus NPU subtotal | 1421.424619 ms | 106.942510 ms |
| CPU convolution subtotal, final warmed call | Not separately measured | 46.857780 ms |
| Forward during live capture | About 1550 ms | About 172–173 ms |
| Displayed result interval | About 1580 ms | 480 ms |

The residual includes CPU padding/ReLU, quantize/dequantize, runtime dispatch
and memory traffic. It is not an isolated arithmetic benchmark. The CPU and
other-work row subtracts independently measured medians. CPU convolution
timing includes adapter dispatch and coefficient loads, excludes NPU execution,
and describes the last warmed call rather than an invented median.

## Computation time versus audio duration

The 96x64 log-mel patch represents **975 ms of 16 kHz audio**. Its windows are
400 samples long, spaced 160 samples apart. The streaming frontend needs
another 80-sample (5 ms) lookahead. Forward time is the subsequent computation
time for that ready patch, not the recording duration.

The first prediction therefore needs approximately 980 ms of capture plus
inference and display time. Later patches advance every 480 ms while capture
and computation overlap. The optimized version processes every scheduled
patch in the measured run; the original version coalesced pending windows.

## Evidence and implementation

Live inspection paused the event thread inside ExecuTorch's generic
`conv2d_impl<float>` at `op_convolution.cpp:129`, loading a weight in its inner
kernel loop. Together with the roughly 96% non-NPU fraction in the original
benchmark, this justified optimizing the float convolution path.

Project-owned [helium/conv3x3.h](helium/conv3x3.h) processes four output spatial
positions per MVE vector. It handles contiguous planes and channel-interleaved
layouts using physical tensor strides, contiguous or gather loads, and
contiguous or scatter stores. A scalar remainder handles widths not divisible
by four without overreading. The first 12 convolution shapes execute vector
loops; the last two depthwise shapes have output width two and use only that
scalar remainder. Each output channel stages just its nine float
coefficients (36 bytes) for reuse across the plane, reducing repeated uncached
OSPI reads and generic index calculations. Bias is added after the nine taps.
The gain combines vector computation, simpler indexing and coefficient reuse;
it must not be attributed solely to SIMD arithmetic.

[helium/convolution.cpp](helium/convolution.cpp) accepts only float 3x3,
dilation-one, explicitly padded, nontransposed, batch-one convolutions with
stride one/two, either depthwise multiplier one or a one-input-channel first
convolution. It checks shapes, dtypes and strides before dispatch. Other cases
call the unchanged pack's portable convolution, including its normal output
shape handling. No imported library or pack was edited, and no inline assembly
was added. Firmware compilation requires floating-point MVE.

The repository-owned `EmbeddedModule` now accepts an optional method kernel
registry before loading methods. YAMNet installs one convolution entry using
ExecuTorch 1.4.1's `Program::load_method` registry argument. Other operators use
the global pack registry, and other applications retain the empty default.
The registry has static lifetime; replacement after method load is rejected.
The generated AI layer did not need regeneration: the model, operator set,
compiler/storage settings and PTE bytes did not change.

The design follows local wiki guidance
`techniques-profile-guided-helium-optimization`,
`techniques-arm-compiler-mve-target-configuration`,
`techniques-helium-matrix-kernel-layout`, and
`techniques-helium-predication-and-tail-handling` under
`.agents/skills/helium-wiki/wiki/techniques/`. Those pages are source-reported
guidance; the measurements here are independent board results.

## Correctness and runtime validation

The actual MVE kernel and a scalar reference run through **864** combinations
of layout, channel count, output height/width and independent horizontal/vertical
strides at startup. Tests cover widths 1–9, including vector tails, scattered
stores and output guard space. Host checks run the portable reference version;
the same check on Cortex-M55 exercises actual MVE instructions. Startup rejects
a mismatch (-435). Every one of the 40 fixture calls and ten warmed calls
must dispatch all 14 CPU convolutions to the optimized implementation, with no
portable fallback (-436 otherwise). The diagnostic field `vector_calls` counts
those optimized dispatches, including the two scalar-only narrow layers; it
does not count vector instructions. All 14 NPU delegates must complete without
PMU overflow.

After those 50 uninterrupted calls, a breakpoint at `app_main.cpp:19` stopped
before microphone capture. The retained CMSIS snapshot verifies completion,
CRC, all dispatch counters and zero pipeline errors. All 40x521 output scores
were read through CMSIS memory tools at that stop. They are **bit-for-bit
identical** to the original saved portable-CPU board results: maximum
optimization-induced score change **0.0** over **20,840 scores**.

Classification remains **40/40** against saved PTQ, **39/40** against original
TensorFlow, with original top class in board top-five for all 40 patterns.
These are synthetic numerical checks (38 independent patterns plus two shared
controls), not labelled-dataset accuracy. The pre-existing strict 0.005 score
parity failure remains separately **FAIL(-405)**: maximum board/PTQ error
0.064208984 and board/TensorFlow error 0.118445352. No threshold was relaxed.

The breakpoint was removed before starting audio. An uninterrupted live run
then reached **150 inference/LCD updates**, **7,270 captured blocks** and
**7,268 valid feature frames**. Skipped windows, deadline misses, capture drops,
overflows, request errors and pipeline errors were all **zero**. Consecutive
ten-result health reports advance by about 480 audio blocks (4.8 seconds),
confirming the 480 ms result interval. Unused audio/event stack space was
31,824/14,376 bytes. The target was left running, with no breakpoints and the
serial port released.

The initial d-98 launch/reset produced driver errors before a CPU convolution
ran; it supplies no benchmark evidence. A clean session recovered. Final
figures use b-100/d-101 continued from startup main and exclude that failed
launch and pre-debug execution.

## Memory, provenance and recovery

Method/temp peaks remain **829,016 / 298,752 bytes**, with reservations
1 MiB / 512 KiB. CMSIS reports SRAM0 3,108,880 bytes including padding and both
framebuffers, DTCM RW state 258,712 bytes, and MRAM load 1,871,600 bytes including
padding. The latter leaves 225,552 bytes in the application's 2 MiB MRAM region.
The nine-coefficient local array and startup differential-test stack storage
do not add a persistent model/activation arena.

Firmware SHA256:
`11e4e7df542c857a87c473df2e29d6a2d5909981a1c27c5c80fa681f699580ae`.
Unchanged PTE SHA256:
`bd5c2b892839f8d42467f284a448cbc35a2510a4930959ae1dd5e4987deb86b4`.
AC6 6.24.0, `-O3 -ffast-math`, RTX5 5.9.1, ExecuTorch 1.4.1,
CMSIS-Ethos-U 1.0.0 and Ensemble 2.2.1 were used.

Retained evidence:

- [Raw UART](model/helium_release_uart.txt), [CMSIS snapshot](model/helium_release_cmsis.json),
  [provenance/source hashes](model/helium_release_provenance.json).
- [Raw score-memory reads](model/helium_scores_memory.json),
  [decoded scores](model/helium_board_scores.npz), [metrics](model/helium_metrics.json).
- [Current benchmark](model/benchmark.yml), [portable baseline](model/benchmark_portable_cpu.yml).
- [Original NPU failure shapes and recovery](model/ETHOS_CONVOLUTION_REPRO.md).

Rebuild derived evidence without board access:

```powershell
.venv/Scripts/python.exe projects/yamnet/model/analyze_helium.py
.venv/Scripts/python.exe projects/yamnet/model/summarize_benchmark.py
.venv/Scripts/python.exe scripts/validate_benchmarks.py projects/yamnet/model/benchmark.yml
```

Build/load the selected `DevKit-E8@YAMNet_Release` through CMSIS tools. OSPI
reprogramming is unnecessary for this CPU-only change when the existing PTE CRC
passes. Original conversion, hardware investigation and validation records are
historical evidence and remain intact; use this report for the current kernel
performance and linked memory figures.
