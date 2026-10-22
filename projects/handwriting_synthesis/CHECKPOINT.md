# Handwriting synthesis port checkpoint

Source: https://github.com/sjvasquez/handwriting-synthesis
Checked revision: 5f58984c3bc793bd8930bad7012cd2008d4a4a66

## Long-term goal

Port pretrained handwriting synthesis through PyTorch and ExecuTorch/Vela
to Alif E8 DevKit Ethos-U85. Accept a sentence over UART, start on Return,
clear the previous sentence, and animate white handwriting on a black
landscape LCD. Wrap excess characters onto successive lines within the longest
landscape dimension so the full sentence is visible when it fits vertically.
Bound/report excessive line count rather than squeezing 75 characters into one line.

## Current state

COMPLETE: conversion, benchmarking, coding, final validation and focused review PASS.
The final b-41 Release firmware is RUNNING and ready for UART input.
Release b-41/d-42 ran the same 75-character input from a fresh READY,
generation 1 / seed 42, with dirty-region copying/cache cleaning. Startup CRC
and all fixtures passed. Final firmware SHA256 is
`46820402b7667737a0a622446cbcd49c0aefbbe1f68f115e21683d48c69c8202`.
`model/demo_guard_memory.json` reports 515,984 bytes MRAM load, 1,732,624
bytes SRAM0 and 164,724 bytes DTCM RW. The implementation fits with all
heap/stack regions. Identical full-sentence validation completed: all 1,739
generated results processed on three lines in 61.013 seconds (28.50 points/s), 14.23% faster
throughput than b-35's full-copy implementation. Actual submissions 1,678;
copy 813,300 bytes and cache clean 2,309,152 bytes, versus full-frame equivalents
1,335,552,000 and 1,336,320,000 bytes. Final startup/pipeline/app/event/source/
LCD/ink/UART error and drop counters are zero; OSPI 100 MHz/divider 4 verified.
Raw final evidence is model/demo_guard_75char_uart_part1.json and
demo_guard_75char_live.json. Final acceptance: nine accepted sentences/nine
clears, two deliberate rejections, three normalizations, six stale results
correctly suppressed; requests=acknowledgments=1889, generated=processed=1881.
The final Hello completed. Startup/pipeline/app/event/source/LCD/ink/UART error
and drop counters remain zero. Host checks inject an LCD acquire failure and
verify its returned/preserved error, in addition to full-frame pixel equivalence,
ownership/cache bounds and UART/editor/concurrency coverage. README and
LIVE_VALIDATION.md are complete; catalog entry generated; benchmark validation
passes. The b-41 Release target is RUNNING with breakpoints cleared and COM7
closed/released. Coding has released board ownership. Review may inspect files;
coordinate with parent before any board action.
The milestone list below preserves earlier measurements and debugging history.

Initial review is saved in REVIEW.md. Memory, LCD dirty ownership/cache spans,
catalog and final hardware evidence passed. It required rejecting/reporting extreme
finite coordinates before unsafe float-to-int conversion or integer rasterizer
arithmetic, adding headers to six project-owned RTE wrappers, clarifying only
new Arm LCD contributions are Apache licensed, and describing generated/processed
points accurately (the `drawn` counter includes pen-up/no-pixel results).
The earlier 1,739-point timings are processed-point throughput, not inked-point
or frame counts; b-39 submitted 1,678 frames including the sentence clear.
These changes and final Release validation are complete as detailed below.
The b-39 evidence and hashes remain preserved as historical records.

Follow-up progress: coordinate/header corrections and the MSVC production-node
suite PASS with /fp:fast. Tests cover +/-floatmax, infinity/NaN, 10,000-step
accumulation on each axis, recovery after rejection, INT_MIN/MAX rasterizer and
brush cases, and display overflow reporting. All 1,952 ordinary fixture points
still match every full-frame reference pixel with identical copy/cache traffic.
IEEE-bit finite checks precede bounded float arithmetic; raster differences
use int64_t before span checks. Six wrapper headers and mixed-origin LCD/UART
configuration contribution notices are corrected. Parent benchmark/assessment
wording now describes processed points including pen-up results. Corrected
Release b-41/d-42 passed startup/CRC/CPU/all five NPU fixtures, repeated the
identical 75-character run (same 1739 processed results, 1678 submissions,
61.013 seconds, copy/cache counts and zero errors), and representative UART
replacement/editing/bounds acceptance. An additional global-state bound test
across wrap-origin resets passed under /fp:fast. The ordinary preview SHA256 is
unchanged: 123ae1d3ec3462c9a71df4fe4fb7c0c1b4b7f458f4cd0b51fb25240f5a086bb7.
LIVE_VALIDATION and README are updated. Coding released board ownership with
Release RUNNING, no breakpoints and COM7 closed. Focused review PASS:
all coordinate, source-notice and evidence-wording findings are resolved;
final b-41 evidence matches the firmware/memory results. See REVIEW.md.
No required porting work remains. Parent independently confirmed the live
session is running; review performed no board operations.

- Analysis complete: `.flow/handwriting-synthesis/ANALYSIS.md`.
- No stated reuse license found for upstream source/checkpoint; issue #16
  supplies no author grant. No GPL license identified for the original model.
- User explicitly instructed continuation with missing original-work licensing
  documented and original references, attribution and copyright preserved.
  This is a repository-rule exception for that work, not an upstream license
  grant or an exception for GPL dependencies. Dependency audit remains pending.
- Estimated 3.63 MB int8 weights exceed the default 2 MiB M55 HP model/code
  region. OSPI selected. Double RGB565 landscape buffers need 1,536,000 bytes.
- OSPI scaffold created in `projects/handwriting_synthesis`, excluding template
  model caches, AI layer, generated graph, benchmark results and RTE outputs.
  Temporary spectrum sources remain until coding; do not claim them as a demo.
- CMSIS environment check PASS: full generated-environment schema validation;
  Toolbox 2.15.0, CMake 3.31.12, Ninja 1.13.2, AC6 6.24.0, GCC 14.3.1.
- Registered and validated separate Handwriting_Debug, Handwriting_Release and Handwriting_ProgramModel target sets.
- Converter compiled the actual model successfully. A small selection of pinned upstream references and the pretrained TensorFlow checkpoint were fetched into the project. Dense methods: lstm1 (476 to 1600), attention (476 to 30), lstm2/lstm3 (876 to 1600), gmm (400 to 121). CPU orchestration retains recurrent/attention/sampling semantics.
- Model conversion evidence: 3,632,431 parameters; independent NumPy vs PyTorch parity over 300 forced steps (GMM max absolute error 2.48e-5). Final full-range a16w8 PTQ reports 0.18–0.44% dense normalized RMSE on held-out generated traces. Host previews show readable phrases; no labeled recognition accuracy claimed.
- Actual project-local AI layer: 3,355,648-byte PTE, five genuine Ethos delegates, no CPU dense fallback. Boundary quantize/dequantize remain CPU. Network schema validation passed. See model/ADAPTER_CONTRACT.md and converter report for full evidence/tolerances.
- Source bound is 40*sentence length (up to 3000 generated steps), not the 1200 style-priming buffer. Alphabet has 73 entries; some uppercase/punctuation/non-ASCII are unsupported.
- Release hardware benchmark PASS on COM7 using the programmed OSPI model. Five dense fixtures agree within one output LSB; independent CPU fixtures have max error 4.77e-7. A stable debugger-start gate avoided launch resets interrupting an active NPU call. After warmup, 64 evolving full strokes on a 75-character sentence measured median 117.017505 ms wall time (8.5457 points/s), including all five dense calls, boundary conversion/copies, gates, 76-position attention, mixture sampling, termination and finite checks. NPU subtotal median 112.8710975 ms (45,148,439 cycles at 400 MHz). UART printing, input, LCD and model initialization are outside this interval.
- Verified genuine PMU activity: 380 invocations/IRQs across dense fixtures, warmup and measured steps, no overflow, active cycles equal total cycles. CRC verified, benchmark complete/test passed, application error zero. Saved raw UART, provenance, dense timing and schema-valid benchmark.yml in model/. Board resumed running idle after PASS.
- Benchmark-only firmware uses 337,616 bytes MRAM and 196,608 bytes reserved SRAM0 pools; engine 20,288 bytes. Method/temp peaks 37,336/4,960 bytes. Shared Vela arena 4,960 bytes counted once; disjoint weights total 3,342,480 bytes within the 3,355,648-byte PTE. LCD is not allocated in this harness yet; coding must verify combined memory.
- User asked whether the model is too large for MRAM. Confirmed actual PTE 3.20 MiB exceeds the current 2 MiB HP code/model region. Retain OSPI model storage and MRAM firmware.
- Benchmark report and exact measured harness preserved in model/BENCHMARK.md and model/benchmark_firmware/. Root benchmark schema suite passes all 17 tests, including per-method memory-region union accounting and stroke input size. Coding agent is now active and owns board access, implementing the final CMSIS Stream UART/LCD demo using the measured full-step latency.
- Final-demo graph generation passes from both root and project directories: three dataflow firings and 120-byte FIFOs. UART uses a 512-byte interrupt ring with its callback installed during the first stdio initialization. The pipeline has a single-in-flight synthesis request/ack, node-owned generation invalidation, and incremental white-stroke rendering at fixed scale with character/pen-lift-aware wrapping. Standard project Stream RTE shims restored; app-local event pointer transport adapted to the Stream value variant using persistent request/result storage.
- Final-demo Release build b-26 PASS. Build tool reports 512,592 bytes MRAM, 1,732,624 of 4,194,304 bytes SRAM0 including 1,536,000-byte framebuffers and 196,608-byte pools; DTCM RW 164,648 bytes plus fitting heap/stack reservations. Combined memory fits.
- Host checks compile actual production editor/dispatcher/display/layout wrappers with MSVC: PASS editing, CRLF, 75/76-character bounds, QXZ normalization, unsupported input, single-in-flight/no queue flood, replacement invalidation, white/black pixels and landscape rotation. 1,952 real quantized trajectory points wrapped over three lines. Coding agent now performing final Release board acceptance; do not yet claim hardware demo success.
- First board attempt stopped before LCD initialization: project-owned RTE_Device.h still selected blocking UART4 reception. Conditional interrupt mode added only for BOARD_UART_ASYNC_RX; b-28 rebuilding. Parent preview inspection found line overlap and potential silent vertical clipping; coding agent asked to correct/report this. Parent also found unsupported-character/backspace recovery bug and requested ordered edit-buffer fix and host regression. User reported seeing nothing on LCD; explained benchmark had no display and demo had not reached LCD initialization yet. Inform them once a real sentence is being displayed.
- Release b-29/d-30 now runs autonomously. CRC, CPU and all five dense startup checks PASS. UART `Hello`+CR produced 79 generated/drawn points, 80 presented frames and one clear; pipeline/LCD/event/deadline/ink-overflow/UART-ring error counters zero. Target running. User explicitly confirmed visible white handwriting on black in landscape. Physical display question is answered; none pending.
- Coding fixes include ordered invalid-character editing recovery, copyright headers, 140-pixel spacing with a three-line limit and explicit vertical-ink overflow reporting. Remaining before handoff: UART stdout truncates under interrupt mode because vendor GetTxCount can return a stale count; board-owned adapter needs tx_busy completion wait, followed by final rebuild/load and replacement/max-length/multiline acceptance.
- Latest user steering: 25 MHz animation is too slow; increase OSPI frequency for this demo. Parent verified current OSPI_CLOCK_MHZ=25 and read CMSIS-indexed fitted IS25WX01G documentation (web/issi-com-cn/bdc4cde0f2eb, RevA1 2025-08-14 pp24-25). Existing board API e8_flash_set_read_clock supports guarded 25/50/100 MHz changes with retained DDR/16 dummy clocks and a verified part ID. Coding instructed to use 100 MHz locally before model/NPU reads, leave programming/default config unchanged, verify actual divider/CRC/dense fixtures and save a new full-stroke benchmark while preserving the 25 MHz baseline.
- TX adapter build b-32 PASS. Project uses a separate Board_HP-U85-UART.clayer.yml so vendor polling stdout/err components are replaced only for this application; original shared board layer restored. Build b-33 in progress applies APP_READ_CLOCK_HZ=100000000 before CRC/runtime and records 64 warmed evolving full steps from the first 75-character input, with per-step PMU invocation/cycle/overflow checks. ProgramModel stays at safe startup 25 MHz. Final 100 MHz performance not yet claimed.
- Release b-33/d-34 PASS at verified 100 MHz (400 MHz AXI / divider 4). Full PTE CRC cf5a91ed, CPU fixture and all five dense fixtures pass. Uninterrupted 64-step measurement: Engine::step median 33.524935 ms, NPU subtotal 30.30938 ms; 3.49x full-step speedup versus the preserved 25 MHz baseline. Actual 75-character sentence completed across three lines; wraps=2, screen_full=0, ink_overflows=0, pipeline/event/LCD/deadline errors zero.
- User saw no clear visible acceleration and asked about the bottleneck. Measured current display throughput approximately 14.3 points/s (367 points over about 25.7 s including debugger snapshot overhead): inference and up-to-33 ms LCD waits are serialized on one event thread. Coding is splitting the shared LCD API into submit and safe-buffer-release wait, preserving existing blocking lcd_present for other apps. Handwriting will wait immediately before buffer reuse so next inference overlaps the existing two-event safety interval. All writes including replacement clear and final flush/error checks must preserve buffer ownership. Rebuild/revalidate this optimization before final handoff.
- Split submit/release Release build b-35 PASS; matching d-36 loading. Host checks pass, including explicit framebuffer immutability until release, replacement clear safety and graph generation from both directories (three firings, 120-byte FIFOs). Actual final frame-rate and UART/multiline acceptance remain in progress.
- b-35/d-36 validation complete: 75-character sentence generated and processed all 1,739 points in 69,698 ms (24.9505 processed points/s), three lines/two wraps, normal completion and all pipeline/display/drop/deadline/overflow errors zero. Model/CPU/dense startup checks passed at observed 100 MHz. Canonical 100 MHz benchmark record now accompanies the immutable 25 MHz baseline; Engine median 33.5217875 ms and NPU 30.30937375 ms (3.49079x complete-step speedup). Raw UART, completion snapshot, binary hashes and the b-35 memory record are preserved. Both summarizers rerun successfully without relabeling the baseline with a later binary; all seven benchmark files validate and all 17 benchmark schema tests pass.
- Latest user request: refresh only a small LCD region on each stroke. Coding has implemented alternating-buffer dirty-region synchronization plus native-row cache cleaning, retaining continuous panel scanout and the conservative safe-release guard. Release b-37 build PASS (firmware SHA256 541e778e18bf6a63f70e85023680c6a7995a285367aa3f13c55876109582abbc). Host checks pass pixel-equivalence against full-frame copying for all 1,952 real trajectory points, damage/cache span bounds and no writes before buffer release. Coding owns board access and is loading/testing b-37; final throughput and UART acceptance pending.

## Completion and preservation

1. Conversion and benchmarking complete. Preserve model caches, provenance, licensing caveat, per-method tolerances and raw evidence. Read model/CONVERSION_REPORT.md and the benchmark report.
2. Coding and follow-up COMPLETE: coordinate-outlier guards, source contribution notices and measurement wording corrected; fast-math regressions and final b-41 Release validation passed. Preserve b-39/b-35/b-22 historical evidence.
3. Focused review PASS. Final corrected Release b-41/d-42 validated and left
   running with no breakpoints and COM7 released. Catalog includes the demo.

Do not add to the catalog before successful conversion, board benchmarking and
final demo review. If conversion, benchmarking or combined memory fails,
document the concrete limitation in the project checkpoint; preserve the work.
The b-39 pre-guard records remain preserved under model/demo_final_*; no old
measurements have been relabeled with the corrected binary.
