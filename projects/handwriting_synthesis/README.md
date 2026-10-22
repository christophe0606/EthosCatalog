---
description: UART-driven animated white handwriting on a black landscape LCD using Ethos-U85 and OSPI weights.
category: text
catalog: true
---

# Handwriting synthesis

This Alif E8 DevKit demo runs Sean Vasquez's pretrained handwriting synthesis
network through PyTorch, ExecuTorch/Vela and Ethos-U85. Enter a sentence on
UART4 at **115200 baud, 8N1**, then press Return. The new sentence clears the
old canvas and starts drawing white pen strokes on a black **800x480 landscape**
LCD. The user confirmed the initial Release demo's visible handwriting.
Final timing and acceptance evidence are recorded in
[LIVE_VALIDATION.md](LIVE_VALIDATION.md).

![CMSIS Stream graph](generated/app.png)

[Graph PDF](generated/app.pdf), [conversion](model/CONVERSION_REPORT.md),
[benchmarks](model/BENCHMARK.md), [adapter contract](model/ADAPTER_CONTRACT.md)

## Terminal behavior

- Enter **1..75 supported ASCII characters**. CRLF submits once; LF also submits.
- The demo echoes characters while you type; terminal local echo should be off.
  Backspace/Delete erases the displayed character, and Ctrl-U/Escape erases the
  unfinished line. Q/X/Z echo as their normalized lowercase characters.
- Backspace/Delete removes the last typed byte, including an unsupported byte.
  Ctrl-U or Escape clears the unfinished input. Empty lines do not replace the canvas.
- Uppercase Q, X and Z normalize to lowercase; the accepted line is printed on UART.
- Unsupported punctuation/non-ASCII or more than 75 characters reject the input
  on Return and leave the current sentence running. Backspace can correct either.
- A new accepted sentence cancels unfinished generation. Generation IDs suppress
  stale results; the canvas clears before the new generation's first point.

The demo starts at **legibility bias 1.0**, with initial seed 42. The random
generator continues across sentences, so submitting the same text again gives
a new handwriting sample. Restarting the demo restarts the sequence. Enter commands on
their own lines, then submit the sentence again:

```text
:bias 1.0
:seed 42
abcdefgh
```

`:bias` accepts 0..2; larger values reduce sampling variation and often produce
neater writing. Try 0.75 or 1.25 as alternatives; increasing it is not a guarantee
of correct characters. `:seed 123` explicitly resets the random sequence for the
next sentence; it then continues across later sentences. Repeating `:seed 123`
before the same text and bias reproduces that sample. Settings
apply to the next sentence; they do not interrupt the current one. `:bias` or
`:seed` without a value reports the settings, and `:help` lists commands. Use
`::` to start literal text with a colon. Input echo and editing also work for
commands.

Supported characters, in addition to space, are:

```text
!"#'(),-.0123456789:;?ABCDEFGHIJKLMNOPRSTUVWYabcdefghijklmnopqrstuvwxyz
```

The renderer keeps a fixed 3-pixel/model-unit scale and continues on successive
landscape lines, preferring spaces and attended character changes at pen lifts.
The pen retains fractional coordinates and draws round, antialiased white
strokes with gray edges. Finishing punctuation, including a preceding space and
strokes attended by NUL, can use the reserved right margin before wrapping.
There are three generously spaced lines. An extreme continuous stroke has a
hard edge wrap; this can split a glyph. Excess lines, extreme coordinate outliers
or ink outside the viewport produce UART feedback and diagnostic counters.
Outliers are rejected before arithmetic/conversion, and the next valid point
starts a new stroke connection. Text is never squeezed into one
small line. Stochastic unprimed generation can add trailing strokes, terminate
early or reach the original 40*length bound; text accuracy is not guaranteed.
Float/quantized comparisons and the appearance changes are documented in
[QUALITY_IMPROVEMENTS.md](QUALITY_IMPROVEMENTS.md).

## Execution and storage

CMSIS Stream dataflow reads a 512-byte interrupt UART ring every 5 ms, edits the
sentence, then sends one inference request at a time. The `synthesis` controller
owns recurrent state and sends each stroke through five separate model nodes:
`lstm1 -> attention -> lstm2 -> lstm3 -> gmm`. All five reuse the
`HandwritingNetwork` C++ wrapper with a different method parameter from the shared
PTE module. Each node runs exactly one dense NPU method and its associated CPU
recurrence, attention or sampling; the final node returns to the controller.
All model nodes and the controller execute serially on the event thread.
A stage consumes the ExecuTorch method's output directly before another model
executes. The engine reuses unchanged packed window/coordinate slices between
adjacent stages. Together these remove 20,412 copied bytes per stroke and the
6,400-byte output staging buffer (Engine: 20,288 to 13,888 bytes). The small
recurrent-state copies remain on the CPU in DTCM: measured DMA transfers were
slower. The isolated output-copy comparison saved about 0.05% of step time;
see [transfer measurements and reproduction](TRANSFER_OPTIMIZATION.md).
A completion acknowledgment releases
the next request after the prior result has been rendered. Sentence ownership,
layout, styling and animation are project nodes; the shared board layer supplies
UART/LCD hardware operations. The LCD node submits a VSYNC flip and defers its
two-event buffer-release wait until just before reuse, overlapping inference.
It copies only the prior stroke damage into the alternate buffer and cleans
only changed, aligned cache regions. Pen-up moves do not flip unchanged frames.
Replacement clears the complete canvas; the panel continues scanning normally.

The controller's event outputs are `oev0` (acknowledgment to `send.iev0`),
`oev1` (stroke/clear to `display.iev0`), and `oev2` (work to `lstm1.iev0`).
`gmm.oev0` returns completed work to `synthesis.iev1`; there is no direct
controller self-connection. One retained work item owns the recurrent buffers
throughout the chain, and the sender retains its request until acknowledgment.

The 3,355,648-byte PTE exceeds the current 2 MiB HP MRAM code/model region, so weights
execute from OSPI and firmware remains in MRAM. Two RGB565 buffers use 1,536,000
bytes. Final firmware occupies 515,984 bytes MRAM; SRAM0 uses 1,732,624 of
4,194,304 bytes. The validation report details all memory reservations.
At 100 MHz the complete step takes about 33.52 ms. With LCD release overlapping
inference and partial updates, both b-39 and the guarded b-41 processed 1,739 generated trajectory
points on three lines in 61.013 seconds (28.50 generated points/s). This includes
pen-up movement: 1,677 point events submitted ink updates, plus one canvas clear.
Inference timing and sentence display time have distinct
scopes. The project changes read clock to 100 MHz through the bounded board API after safe
25 MHz OSPI startup and before CRC verification/model loading. Other applications
and ProgramModel preparation retain their existing clock. All five dense fixtures
and independent CPU equations are checked at startup; wait for Handwriting READY
on UART before typing.

The 25 MHz reference measured 117.017505 ms per complete stroke step, including CPU
recurrence/attention/sampling and five dense methods. This is not a whole-sentence
time. The demo streams points as they arrive. Each 75-character sentence also
records 64 warmed samples. After the graph split, `PROFILE_TICKS` sums the five
model-node processing intervals, including CPU recurrence/attention/sampling and
excluding event dispatch, UART and LCD time; it is no longer one contiguous timer
interval. `PROFILE_CYCLES` still sums all five NPU calls. These are printed once
on UART, without an LCD overlay. Timing and memory figures above describe the
prior validated firmware in `LIVE_VALIDATION.md`, before this graph split.

Select **DevKit-E8@Handwriting_Release** to build/load/debug. Continue from the
initial main breakpoint; startup is autonomous and has no debugger start gate.
Program the project-local model using **Handwriting_ProgramModel** and the
[OSPI programming procedure](../../documentation/alif-e8-flashing.md#program-an-ospi-model).
The PTE did not change when adding this demo or increasing its read clock, so an
already correctly programmed model need not be programmed again.

Generate from either directory with the repository Python environment:

```powershell
.venv/Scripts/python.exe projects/handwriting_synthesis/app.py
# or, from projects/handwriting_synthesis:
../../.venv/Scripts/python.exe app.py
```

Host checks compile the production editor, sender, display and layout wrappers,
compare partial updates against full-frame reference pixels, verify both buffer
ownership and declared cache regions, and exercise editing/backpressure.
The model-chain checks compile the production controller, five wrapper instances
and recurrence code with deterministic dense outputs. They check input packing,
one method per node, stage order, cancellation between nodes, EOS/step limits,
result-before-ACK ordering, runtime errors and queue overflow:

```powershell
.venv/Scripts/python.exe tests/run_handwriting_tests.py --vcvars path/to/vcvars64.bat
# or use --cxx with an installed host C++17 compiler
.venv/Scripts/python.exe -m unittest discover -s tests -p test_handwriting_graph.py
```

[Host rendering preview](host-layout-preview.png) uses 1,952 saved quantized
trajectory points across three lines; character boundaries in that geometry
fixture are estimates. This supplements actual board input/display validation.

## Original-work notice

Original author: **Sean Vasquez (`sjvasquez`)**,
<https://github.com/sjvasquez/handwriting-synthesis>, pinned revision
`5f58984c3bc793bd8930bad7012cd2008d4a4a66`.

The original source and pretrained weights have **no stated reuse license** and
are not covered by this project's Apache-2.0 notices. The user explicitly directed
this experimental port to continue with the missing-license status documented and
original attribution preserved. That instruction is not an upstream license grant.
See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
