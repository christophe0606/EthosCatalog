# Hardware validation

Validated 2026-10-22 on the Alif E8 DevKit, Cortex-M55 HP and Ethos-U85,
with the project-local 3,355,648-byte PTE in OSPI. All board operations used the
CMSIS Developer Assistant. The user confirmed white handwriting on the black
landscape LCD during bring-up, then observed the accelerated animation.

## Review-fix Release b-41

The coordinate-guard and attribution fixes were rebuilt as **b-41**, loaded
with matching symbols by **d-42**, and validated on the board. AXF SHA256:
`46820402b7667737a0a622446cbcd49c0aefbbe1f68f115e21683d48c69c8202`.
The PTE is unchanged. Fresh [startup UART](model/demo_guard_startup.json)
records 100 MHz OSPI, 400 MHz AXI/divider 4, full CRC `cf5a91ed`, independent
CPU maximum error 0.000000477, and all five dense NPU fixtures passing.

The identical 75-character input below, seed 42 and generation 1, completed
without debugger interruption: **1,739 generated/processed points in 61.013
seconds**, or **28.50 generated points/s**, on three lines with two wraps.
This includes pen-up results; **1,677 point events submitted ink updates**,
and the initial clear made **1,678 total frame submissions**. No clipping,
outlier rejection, excessive height or step-limit termination occurred.
Copy/cache-clean totals remained 813,300 / 2,309,152 bytes. The guarded layout
therefore preserved the ordinary trajectory and performance of the b-39 run.

[New full-run UART](model/demo_guard_75char_uart_part1.json) and
[live counters/readbacks](model/demo_guard_75char_live.json) record requests,
acknowledgments and inferences all equal to 1,740. Pipeline/app error, event
drops, source-read/LCD deadline misses, ink overflow, screen full and UART
drop/error counters were zero. Actual OSPI readback was 100,000,000 Hz,
with BAUDR 4. The new [memory report](model/demo_guard_memory.json) records
**515,984 bytes MRAM** and unchanged **1,732,624 bytes SRAM0**; DTCM and
heap/stack reservations below are unchanged.

The guarded build also repeated CR/CRLF, active replacement, unsupported-byte
editing, overflow rejection/correction, normalization, Ctrl-U and empty-input
checks. [Exact commands, UART and final counters](model/demo_guard_uart_acceptance.json)
recorded nine accepted sentences and nine clears, two deliberate rejections,
three normalizations and six stale results suppressed. Requests/acknowledgments
were both 1,889; generated/processed counts were both 1,881. The final Hello
completed 101 generated points in 3.573 seconds. All pipeline/app, event,
source/LCD deadline, clipped/outlier ink and UART drop/error counters remained
zero. The target was left running with that completed Hello, no breakpoints,
and COM7 released. This is the final validated image.

The preserved `demo_final_*` files and sections below identify the earlier
b-39 image explicitly; their hashes and measurements have not been relabeled.

## Recorded b-39 firmware

- Context: `handwriting_synthesis.Release+DevKit-E8`;
  target set: `DevKit-E8@Handwriting_Release`.
- Build **b-39**, matching load/debug **d-40**, AC6 6.24.0, RTX5, CPU/NPU 400 MHz.
- AXF SHA256: `f182ef78becf5af9fca308b7e3309e1838ff7d2b462d669f3e721cc68eaa207c`.
- PTE SHA256: `8991565f2c6ef468942789f458d6809f6879a1315c9610035b5e7a6f4390f3d3`.
- Startup selected 100 MHz OSPI through `e8_flash_set_read_clock`. Live debugger
  readback confirmed `ospi_clock=100000000` and `BAUDR=4`, with 400 MHz AXI.
  The board-wide startup default remains 25 MHz.
- Full model CRC `cf5a91ed` passed after the clock change. Independent CPU
  equations had maximum error 0.000000477 against tolerance 0.000003.
  All five genuine NPU dense fixtures passed their per-method tolerances.
  Method/temp high-water marks were 37,336 / 4,960 bytes.
- Final target was left **running**, with no breakpoints, a completed white
  “Hello” on the black canvas, and the MCP-owned COM7 connection closed so a
  normal terminal can connect at 115200 baud, 8N1.

[Raw startup](model/demo_final_startup.json) preserves the actual UART chunks.
Loader output was cleared at the initial main breakpoint before continuing;
input was sent only after the fresh Handwriting READY. Autonomous firmware
startup has no benchmark debugger gate.

## Full sentence and performance

The preserved b-39 run used exactly:

```text
The quick brown fox writes white words on black. This sentence fills a LCD.
```

This is 75 characters, submitted with CRLF, seed 42, generation 1. No debugger
halt or read interrupted the run. It processed **1,739 generated trajectory points
in 61.013 seconds**, or **28.50 generated points/s**, on **three lines with two wraps**.
There was no clipped ink, excessive line count or model step-limit termination.
There were 1,677 point events submitting ink updates and 1,678 frame submissions
including the clear. The legacy `drawn` counter counts processed generated
results, including pen-up movement; it is not an inked-point or frame count.
A completion flush waits for the final pending presentation.

The earlier 100 MHz build b-35, with the same input and seed and full-buffer
copies/cleaning, took 69.698 seconds (24.9505 points/s). Partial updates therefore
raised observed point throughput by **14.23%**. The renderer copied **813,300
bytes**, including alternate-buffer synchronization after the clear, versus
1,335,552,000 bytes for the prior full-copy policy. It cleaned **2,309,152 bytes**,
including full clear/synchronization, versus 1,336,320,000 bytes for the prior
full-clean policy. These are software copy/cache traffic counts, not a change
to the panel's continuous scanout rate.

The model remains the main floor: [benchmark evidence](model/BENCHMARK.md)
separately records the 25 MHz reference and the 100 MHz full-step timings.
The final run also preserved 64 warmed full Engine::step samples in its raw UART
record. Those intervals exclude LCD, input and queue waiting; they must not be
reported as visible animation throughput.

The two-buffer driver still requires two end-of-active-video events before the
previous buffer can be written. Splitting submission from release waiting
allows inference to overlap that interval. The maximum deferred wait during
the uninterrupted long run was 574 timer ticks (1.435 microseconds); maximum
submission time was 169,704 ticks (0.42426 ms), including the full clear.

[Full UART run](model/demo_final_75char_uart.json) and
[live counters/readbacks](model/demo_final_75char_live.json) preserve the evidence.
For this run, requests/acknowledgments/inferences were all 1,740, and startup
verification was 1. All of these counters were zero: pipeline/app error, event
drops, source-read deadline misses, LCD submission deadline misses, ink overflow,
screen full, UART ring drops and UART hardware errors. The source deadline
covers ring draining, not the entire editor/printf execution interval.

## Recorded b-39 input, replacement and error paths

Final b-39 tests exercised short CR input, different CRLF input, replacement
during active NPU generation, ordered editing of unsupported bytes, and bounds:

| Input/action | Observed result |
| --- | --- |
| `Hello<CR>` then `World<CR><LF>` | One accepted sentence and clear per submission |
| `Hi@<BS> there<CR>` | Accepted “Hi there” |
| `a@b<BS><BS>c<CR>` | Accepted “ac” |
| `bad@<CR>` | Rejected; active canvas/generation preserved |
| 76 `a` bytes then Return | Rejected |
| `QXZ<CR>` | Accepted “qxz”; normalization count increased by 3 |
| Unfinished text, Ctrl-U, `Done<CR>` | Accepted “Done” |
| 76 `a` bytes, Backspace, Return | Accepted 75 characters |
| Empty CRLF | No new generation or clear |

The final test sequence had **9 accepted sentences, 9 clears, 2 rejections,
3 normalized characters and 6 stale results suppressed**. Requests and
acknowledgments both ended at 1,916. Generated and processed-result counts both ended at
1,908; the last Hello completed 101 points in 3.573 seconds. Pipeline/app errors,
event drops, source/LCD deadline counters, clipped ink and UART ring/hardware
errors remained zero. Mid-generation cancellation uses atomic generation IDs;
the result is checked again before display consumption.

[Final input commands, UART and live counters](model/demo_final_uart_acceptance.json)
contain exact bytes and results. During earlier b-37 bring-up,
[a 4 KiB framebuffer sample](model/demo_b37_hello_pixels.json) contained only
black/white byte values and included 172 white bytes in the sampled glyph strip.
The same renderer is used by b-39; its change propagates LCD errors reliably.

## Recorded b-39 memory

The [final build-artifact report](model/demo_final_memory.json) gives:

| Resource | Usage/reservation |
| --- | ---: |
| MRAM load image | 515,632 bytes |
| OSPI model | 3,355,648 bytes |
| SRAM0 total load/zero region | 1,732,624 / 4,194,304 bytes |
| Two RGB565 LCD buffers, within SRAM0 | 1,536,000 bytes |
| Reserved method/temp pools, within SRAM0 | 131,072 / 65,536 bytes |
| DTCM initialized/zero data | 164,724 bytes |
| DTCM nonsecure control region | 3,296 bytes |
| DTCM library/application heaps | 98,304 / 32,768 bytes |
| DTCM main stack | 32,768 bytes |

These ranges fit without overlap. Pool peaks and the shared 4,960-byte Vela
arena are included in the reserved pools; weights are already in the PTE.
They must not be added again. The 3.20 MiB PTE exceeds the current 2 MiB HP
MRAM code/model region, while the actual firmware fits there.

## Host and generation checks

Graph generation passed from both repository root and project directory:
three dataflow firings and 120-byte FIFOs. Production-node C++ host checks
passed editor/CRLF/bounds/backpressure/cancellation, RGB565 rotation, readable
three-line layout and viewport checks using 1,952 saved quantized trajectory
points. Every partial-update frame exactly matched a full-frame reference.
Checks also ensured every modified pixel lay inside its declared cache region,
rounded native cache spans stayed within the buffer, and both buffers remained
immutable until release. An injected acquire failure returned the error and
preserved it in diagnostics, covering the synchronous-clear failure path.

The review-fix host run compiled these production wrappers with MSVC `/fp:fast`.
It also passed both signs of maximum finite float, infinity/NaN, 10,000-step
accumulation on each axis, recovery after rejected coordinates, and extreme
`INT_MIN`/`INT_MAX` raster endpoints, including the extra brush pixel. Rejected
model coordinates incremented `ink_overflows` without changing display pixels.
IEEE-bit checks precede floating arithmetic, which keeps the checks meaningful
with fast-math. State and screen bounds precede `lround`; raster differences
are widened before subtraction and absolute value. The regenerated preview
was byte-identical to the preserved ordinary fixture PNG, SHA256
`123ae1d3ec3462c9a71df4fe4fb7c0c1b4b7f458f4cd0b51fb25240f5a086bb7`.
An additional wrap-origin-reset case reached the separate absolute-state bound
and verified recovery without connecting across the rejected segment.

The host geometry fixture uses estimated character markers; it is not a text
recognition accuracy test. Its preview is [saved here](host-layout-preview.png).
The actual 75-character hardware run above supplies independent multiline
model evidence. Excessive line count is bounded to three lines and reported;
extreme ink outside the viewport is reported rather than silently counted as
fully visible. Long uninterrupted strokes can still split at a hard edge.

The original upstream source/checkpoint has no stated reuse license. The user
authorized documenting that status and preserving Sean Vasquez's attribution;
see [third-party notices](THIRD_PARTY_NOTICES.md). This is not an upstream
license grant.

## Separate model nodes (2026-10-22)

The refactored graph runs `lstm1`, `attention`, `lstm2`, `lstm3` and `gmm`
as five instances of `HandwritingNetwork`, configured with method IDs 0..4.
`HandwritingSynthesis` coordinates the shared recurrence and results, without
executing the model chain itself. Its acknowledgment output `oev0` returns to
the sender; `oev2` starts the model chain, whose final output returns on `iev1`.

CMSIS Release builds b-43 and b-44 passed for
`DevKit-E8@Handwriting_Release`. The updated firmware was loaded and run on
M55_HP. Startup passed the unchanged PTE CRC (`cf5a91ed`), CPU fixtures and all
five dense NPU fixtures, and reached `Handwriting READY`. All five model-node
instances and the synthesis controller were present in the live scheduler.

Two `Hello` sentences each completed with 101 points on one line (3,568 and
3,573 RTOS ticks at 1 kHz). Between those runs, a 44-character sentence was
replaced while generation was active. The final live counters were:

| Counter | Value |
| --- | ---: |
| Sentences / canvas clears | 3 / 3 |
| Requests / acknowledgments / inferences | 514 / 514 / 514 |
| Generated / drawn points | 511 / 511 |
| Completed sentences | 2 |
| Discarded stale results | 1 |
| Pipeline error / event drops / ink overflows | 0 / 0 / 0 |

The target was resumed and the owned serial port released. No new visual
acceptance or 75-character throughput measurement is claimed for this refactor.
The historical performance/memory results earlier in this document belong to
the previous graph. After the split, `PROFILE_TICKS` sums the five model-node
processing intervals; event dispatch time is excluded.

Host C++ checks passed the existing 1,952-point rendering fixture and a new
production-wrapper/controller test with deterministic dense outputs. The latter
checks input packing and recurrence across strokes, one method per node, stage
order, in-flight request protection, replacement between nodes, result-before-ACK
ordering, EOS/step limits, dense errors and queue overflow. Two graph regression
tests verify all event ports and shared-wrapper code generation. Graph generation
passed from both working directories with three dataflow firings and 120-byte
FIFO storage; the seven shared graph tests and catalog test also passed.
