# Release board benchmark

The original 25 MHz measurements below are retained as a baseline. The final
demo selects 100 MHz for its own OSPI reads; programming and other applications
retain the shared 25 MHz setting.

## 100 MHz production demo measurement

Release build b-35 / execution d-36 verified a 400 MHz AXI clock divided by four,
the unchanged PTE CRC, all five startup dense fixtures, and the CPU recurrence
fixture. One warmup followed by 64 evolving steps on the same 75-character
input gave a complete `Engine::step` median of **33.521788 ms**, including
**30.309374 ms** of NPU activity. This is **3.49 times faster** than the 25 MHz
complete-step baseline. Each measured step passed the production checks for
five NPU invocations, matching cycle/active counts and no PMU overflow. These
are full-step measurements; the ten-sample per-method timings below were not
repeated at 100 MHz.

With inference overlapping the conservative LCD buffer-release wait, the full
sentence generated and processed 1,739 points in 69,698 ms: **24.95 processed
points/second**, across three lines with two wraps. This count includes pen-up
results that change no pixels; it is not a count of inked points or LCD frames.
All generated results were processed,
normal completion was recorded, and inference, display, timeout, stale-result,
coordinate and screen-overflow error counters were zero. This b-35 binary
still copied and cache-cleaned the full framebuffer on every point. Later
dirty-region improvements have separate validation and do not change this
historical result. The engine timing excludes drawing; the sentence timing
includes the complete production pipeline.

The build-artefact memory report for this measured demo records 514,704 bytes
of MRAM load and 1,732,624 bytes in SRAM0, including both 768,000-byte LCD
buffers and the reserved runtime pools. `demo_memory.json` preserves this
b-35 report; later builds have separate memory reports.

Raw evidence is preserved in `benchmark_100mhz_demo_startup.json`,
`benchmark_100mhz_demo_profile.json` and `demo_b35_75char_live.json`.
`benchmark_100mhz_provenance.json` records the measured firmware SHA256
`46e1296da296f0341d1abe4c3418466ad94c16389084779b3ef0173bc1e92040`.
Run `summarize_demo_benchmark.py` to validate these records and regenerate the
100 MHz YAML entry, canonical UART transcript and summary. The baseline
summarizer preserves this separate entry and uses its archived firmware hash.

## 25 MHz baseline

The actual converted network passed on Alif DevKit-E8 M55 HP / Ethos-U85 256
MACs, 22 October 2026. CPU, AXI/NPU and RTX system timer were all read as
400,000,000 Hz. OSPI1 weights use the project's existing 25 MHz configuration.
AC6 6.24.0 compiled the application, adapter and hardware initialization with
`-O3 -ffast-math`, checked in their generated compile commands. Runtime is
ExecuTorch 1.4.1, RTX5 5.9.1, Ensemble pack 2.2.1. The actually selected driver
is CMSIS-Ethos-U 1.0.0 / Generic U85 component 2.0.0, as recorded in the cbuild;
the solution also declares a separate ethos-u-core-driver pack.

## Measured latency

| Work | NPU median ms | Wall median ms |
| --- | ---: | ---: |
| lstm1, 476 to 1600 | 23.524718 | 23.967008 |
| attention, 476 to 30 | 0.532233 | 0.936733 |
| lstm2, 876 to 1600 | 43.409748 | 43.903395 |
| lstm3, 876 to 1600 | 43.700960 | 44.202020 |
| gmm, 400 to 121 | 1.703443 | 2.104643 |
| Complete recurrent stroke step | **112.871098** | **117.017505** |

Each dense method had one warmup and ten measured calls using the converter's
held-out input fixture. The complete step had one warmup followed by 64 measured,
evolving steps, seed 42, on 75 supported characters plus NUL. Attention evaluates
all 76 positions. Full-step PMU median is 45,148,439 cycles; range
45,148,419 to 45,148,455. Wall time includes input assembly, all five float-boundary
calls, output copies, three CPU LSTM updates, attention and window construction,
GMM/categorical/normal/pen sampling, independent termination check, finite-state
validation and PMU hooks. Dense wall times include tensor setup, execution and
output copy, and exclude their subsequent output finite check. UART printing
occurs after the measured interval. LCD rendering, UART input, model CRC/load and
method warmups are excluded. No debugger inspection or breakpoint interrupted
the measured run.

This is roughly **8.55 generated points/second** before display costs. Publish
and draw points as inference produces them; precomputing an entire sentence
could postpone its first visible stroke for minutes. Keep UART reception
independent of the long NPU call. This benchmark has not measured the final
LCD/dataflow load or a full generated sentence, and its samples cover the early
65 steps, not all recurrent states. Wall samples ranged from 116.983670 to
117.186640 ms, mean 117.025552 ms.

## Numerical and hardware checks

All five dense fixtures passed on warmup and every measured sample. Maximum
absolute differences were lstm1 0.001420975, attention 0, lstm2 0.001415491,
lstm3 0.001286507, gmm 0.001420736: approximately one output LSB, within the
converter's four-LSB board bounds. This validates the deployed quantized graph,
not accuracy against a labeled handwriting recognition dataset.

Independent double-precision NumPy gate/window fixtures passed with maximum
error 0.000000477 against 0.000003 tolerance. They cover i/j/f/o gate order,
forget bias +1, retained cell state, softplus attention, beta floor, kappa /25,
repeated character scatter and the terminal NUL position. Every generated step
kept finite recurrent state and feedback. Sampling follows the documented
probability laws with xorshift/Box-Muller; it does not reproduce NumPy's RNG
sequence. Float32 rho uses the nearest representable value inside +/-1 because
1e-8 is smaller than float32 spacing at 1.

Each dense call had one real NPU invocation; each full step had five. PMCCNTR
equaled PMEVCNTR0 NPU_ACTIVE and overflow remained zero. The post-run interrupt
count was 380 = 5*(1+10) + 5*(1+64). Debugger validation found
`handwriting_test_passed=1`, `handwriting_benchmark_complete=1`,
`pipeline_error=0`, `model_verified=1`, CRC32 3478819309. The target was resumed
and left running after inspection.

## Observed memory

| Resource | Bytes | Accounting |
| --- | ---: | --- |
| Complete OSPI PTE | 3,355,648 | Includes all weights regions |
| Disjoint read-only Ethos regions, sum of five | 3,342,480 | Already inside PTE |
| Shared Ethos scratch arena | 4,960 | Maximum method arena; counted once |
| ExecuTorch method allocator peak | 37,336 | All five methods retained |
| ExecuTorch temporary allocator peak | 4,960 | Includes shared scratch arena |
| Reserved method / temporary pools | 131,072 / 65,536 | SRAM0, ample unused capacity |
| Engine object | 20,288 | State, 876-float assembly, 1600-float output scratch |

Per-method read-only regions are 696,672 / 15,424 / 1,285,856 / 1,294,432 /
50,096 bytes in dependency order. Address intervals are disjoint inside the
PTE. Per-method arenas are 4,160 / 1,024 / 4,960 / 4,960 / 1,056 bytes; all use
the same 0x02020000 base sequentially. The last callback describes only gmm;
it is not the entire model. The YAML reports the sum of disjoint read-only
regions and maximum shared arena. It must not add model, weights, pools and
arenas as independent allocations.

For the logical full-step interface, input/output are one three-float stroke;
the YAML input tensor is 12 bytes. Dense input assembly is 3,504 bytes and is
included in the Engine object, with recurrent state and other scratch. Text
encoding and recurrent state are persistent internal state. The board memory
tool reports benchmark firmware MRAM load 337,616 bytes, SRAM0 reserved pools
196,608 bytes plus 16-byte padding, and total RAM about 524,960 bytes including
heaps/stacks/pools. No LCD buffers are allocated by this benchmark. Final demo
memory must be checked after adding two 768,000-byte RGB565 landscape buffers,
UART queues, graph FIFOs and display processing.

## Programming, startup and reproduction

Used explicit target sets throughout: `Handwriting_ProgramModel` build b-16,
verified `model_programming_ready=1`, `app_stage=5`, `app_error=0`,
`app_service_error=0` at the programming wait. Detached without resetting,
freed the probe, then the CMSIS flash tool programmed 3,358,720 rounded bytes
from the model-only run description. Normal startup enables XIP and verifies
the full PTE CRC before runtime construction.

Initial automatic Load+Run/debug connection sequences restarted the CPU during
NPU execution, leaving an outstanding job and subsequent -421/busy failures.
Those partial captures are retained as `benchmark_uart_attempt*.json` and
`benchmark_uart_old_remaining.json`; they are excluded from published timing.
Live inspection showed CRC correct, no CPU fault, and successful individual
NPU results in a partial run. The stable benchmark adds a deliberate start
gate, keeping the NPU idle while CMSIS/debugger startup resets settle.

To repeat: build/load/debug `DevKit-E8@Handwriting_Release`, wait until its
debugger is fully connected, continue past main, pause at the application wait,
set `handwriting_benchmark_start = 1`, then continue. Collect COM7 UART4 at
115200 8N1 without halting until `Test_result: PASS`. This benchmark-only gate
must be removed/replaced by the coding agent for the autonomous UART demo.
Final measurement build was b-22 and execution d-23.

`benchmark_uart_part1.json` and `benchmark_uart_part2.json` preserve the actual
CMSIS serial tool results for the uninterrupted run. `benchmark_uart.txt`
joins their UART payloads; `benchmark_dense_methods.json` retains every dense
sample and region; `benchmark_memory.json` records the build-artefact tool
result; `benchmark_provenance.json` contains firmware, PTE and source hashes.
`benchmark_firmware/` archives the seven project firmware/header/configuration
files at measurement time with a relative-path SHA256 manifest, so later demo
edits do not erase the measured harness. CPU and dense fixture headers stay in
this model directory. Shared runtime/board files are identified by source hashes.
`summarize_benchmark.py` regenerates the observed YAML/statistics from the
preserved raw run and checks sample completeness, invocations and region
accounting. Validate with `scripts/validate_benchmarks.py`.

Firmware SHA256:
`375531b1e4d5f481cc1430cb066220c4747605140e1f15b7aa8a5b6c9fb41bea`.
PTE SHA256:
`8991565f2c6ef468942789f458d6809f6879a1315c9610035b5e7a6f4390f3d3`.

## Runtime handoff

Reuse `nodes_impl/handwriting_engine.hpp/.cpp`. Construct `Engine` on persistent
storage with the initialized `EmbeddedModule*`; the object is 20,288 bytes and
should not be copied onto a small thread stack. Its public header forward
declares ExecuTorch types to avoid exposing runtime enums to CMSIS Stream nodes.
One inference thread exclusively owns the module, engine and recurrent state.
`begin(sentence,seed)` validates 1..75 supported characters, encodes NUL,
zeros all state and starts previous stroke [0,0,1]. `step(point)` returns 0
for a point, 1 for source normal completion (five calls), 2 for 40*length
truncation before inference (zero calls), or a negative error. `dense()` supports
independent fixture checks. There is no hidden model-loading or allocator copy
in the engine. Integration should preserve all five cached methods and the
same NPU-visible pool placement. See `ADAPTER_CONTRACT.md` and the flow's
`DEMO_REQUIREMENTS.md` for rendering, UART, supported-character and termination
details.
