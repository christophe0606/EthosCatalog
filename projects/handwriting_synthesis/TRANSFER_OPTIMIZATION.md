# Recurrent-state transfer optimization

Measured on DevKit-E8 M55_HP / Ethos-U85 on 2026-10-22, Release with
AC6 6.24.0, OSPI 100 MHz, AXI/system timer 400 MHz.

## Result

Keep small recurrent-state transfers on the CPU. State and packed inputs are
in DTCM, which bypasses the CPU cache. The existing DMA1 driver was slower
at every application size tested, even with initialization and channel
allocation amortized outside the measurements.

The implementation instead removes unnecessary transfers:

- Read each method's output directly for recurrence, attention and sampling:
  19,804 fewer copied bytes per complete stroke step.
- Reuse the window/coordinate slices from lstm1 for attention and from lstm2
  for lstm3: another 608 fewer copied bytes per step.
- Remove the 1,600-float output buffer: Engine shrinks from 20,288 to 13,888
  bytes, saving 6,400 bytes per instance. Startup verification and the Stream
  controller each instantiate an Engine; fixed reserved heap sizes are unchanged.

The model, quantization and PTE are unchanged. CPU LSTM cell updates and
sampling are unchanged. The five model nodes remain separate and serialized.

## CPU versus DMA

Each row sums 64 timed copies after one warmup. CPU includes a noinline memcpy
call and DSB. DMA includes driver Start, generated microcode, source cache
clean, destination maintenance, completion interrupt and bounded polling.
Initialization, power-up and handle allocation occur once outside the timings.
The source/destination offsets reproduce the packed float slices. Every copy
and all destination guard bytes were checked.

| Path | Bytes | Offset | CPU ticks / 64 | DMA ticks / 64 |
| --- | ---: | ---: | ---: | ---: |
| DTCM to DTCM | 12 | 292 | 11,180 | 89,564 |
| DTCM to DTCM | 292 | 0 | 17,152 | 93,289 |
| DTCM to DTCM | 1,600 | 304 | 43,392 | 134,592 |
| DTCM to DTCM | 1,600 | 12 | 43,392 | 134,783 |
| DTCM to DTCM | 1,600 | 1,612 | 43,392 | 135,104 |
| DTCM to DTCM | 1,600 | 1,904 | 43,392 | 135,293 |
| SRAM to DTCM | 120 | 0 | 13,248 | 96,384 |
| SRAM to DTCM | 484 | 0 | 20,032 | 101,632 |
| SRAM to DTCM | 6,400 | 0 | 126,592 | 287,774 |

The 1,600-byte copy takes approximately 1.695 us on the CPU versus 5.26 us
through DMA1. The 6,400-byte copy takes 4.945 us versus 11.24 us. These results
apply to the pack's driver with 4-byte beats and burst length 16, using completion
interrupts. They do not establish a universal DMA crossover threshold or measure
custom/precompiled microcode. These copies are consumed immediately by the next
dependent operation; this measurement does not claim an overlap benefit.

## Direct versus copied output

A single benchmark binary alternates direct output and an optional DTCM copy,
three pairs of 64 warmed full steps. Both modes use the optimized input packing.
The copied path stages the output before checking finiteness, matching the
original consumption order. Each run resets the sentence and seed, performs one
untimed warmup, then hashes all 64 emitted float stroke triplets.

| Pair | Execution order | Copied output ticks / 64 | Direct output ticks / 64 |
| --- | --- | ---: | ---: |
| 0 | copy, direct | 860,607,407 | 860,198,138 |
| 1 | direct, copy | 860,608,506 | 860,191,005 |
| 2 | copy, direct | 860,613,954 | 860,191,807 |

Mean step time is **33.617576 ms copied** versus **33.601314 ms direct**:
about **16.26 us (0.048%)** saved per step in this comparison. This is a small
latency improvement; the substantial result is reduced staging memory and
memory traffic. OSPI/model execution still dominates. These measurements exclude
event dispatch, UART and LCD and do not establish whole-sentence throughput.
The separate earlier-build baseline was 33.577075 ms; differences in code/data
layout make it unsuitable for attributing this small change. Use the paired
same-binary comparison to isolate output copying.

All six trajectories matched the original baseline hash **65bfb6d3**, with
`error=0`. The exact 75-character fixture is:

```text
The quick brown fox jumps over the lazy dog Pack a box with five dozen jugs
```

Seed: 42. Timer: 400,000,000 ticks/s. All five dense startup fixtures, the CPU
equation fixture and model CRC `cf5a91ed` passed. Planned method/temp pool peaks
remain 37,336 / 4,960 bytes.

## Output ownership and validation

`Engine::dense_view` returns a const pointer into loaded method storage. The
local ExecuTorch 1.4.1 `Method::get_outputs` returns shallow copies of internal
tensors; portable Tensor objects do not own TensorImpl or the data. The
EmbeddedModule retains loaded methods and their planned buffers. A stage
consumes the view completely before returning or executing another method.
No borrowed output is retained across model calls or module unload.

The copying `dense()` API remains for startup fixture callers. Method index,
module/input pointers, output type/count/data and finiteness are checked before
publishing a view. Stage order still guards input-slice reuse; failed dense calls
leave recurrence and stage order unchanged for retry. `begin()` forces fresh
packing at stage 0 for a replacement sentence.

Host checks compile the production controller, model wrappers and recurrence
with a reused output-buffer stub. They verify all input slices across steps,
replacement/cancellation, errors, EOS, step bounds, queue behavior and a failed
call/retry at each of the five stages. Both host C++ suites and both graph
regression tests passed. The existing 1,952-point renderer fixture also passed.

Normal firmware validation (benchmark disabled): Release build b-4 and load l-5
succeeded. Startup reached READY with every fixture passing. UART input `Hi`
completed on one line with 49 points in 1,753 ticks at 1 kHz. The board was left
running and COM7 released; this was a functional check, not new visual acceptance
or a controlled whole-sentence performance comparison. The build-artifact tools
reported MRAM LOAD usage 517,488 bytes and SRAM0 LOAD usage 1,732,624 bytes.

## Reproduce

Normal firmware leaves `HANDWRITING_TRANSFER_BENCHMARK` undefined. To run the
comparison, enable its commented define in `handwriting_synthesis.cproject.yml`.
The benchmark source is `tests/handwriting/board_transfers.cpp`; the same define
enables its optional copied-output path in `handwriting_runtime.cpp`.
It runs once after startup fixtures, before the Stream demo starts.

Build `DevKit-E8@Handwriting_Release` using `cmsis_action build`, open UART4's
serial connection at 115200 through the serial tools, and use **cmsis_action
load** for the benchmark. Capture through `Handwriting READY`. The longer
comparison needs roughly 13 seconds for its six step runs. Avoid load_and_debug
for this measurement: CMSIS Load starts firmware before the debugger's init
commands reset it. A reset during an NPU call interrupted an initial benchmark
attempt and caused the following startup to report a busy NPU. A standalone
load completed all six runs; an attach can inspect the target afterward without
a second startup reset.

Disable the define again for the final demo, rebuild/load and verify READY.
The benchmark buffers, DMA setup and copied-output comparison are compiled out
of normal firmware. No PTE regeneration or model flash update is needed.

Hardware references read through the CMSIS documentation tools:
`web/alifsemi-com/a927e530c03e`, ADTS0013 v1.2, pp.42-44 (DMA1/RTSS-HP);
`arm/101051-latest`, r1p1 Issue 03, p.198 section 10.9 (TCM bypasses cache).
