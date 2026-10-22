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
- Backspace/Delete removes the last typed byte, including an unsupported byte.
  Ctrl-U or Escape clears the unfinished input. Empty lines do not replace the canvas.
- Uppercase Q, X and Z normalize to lowercase; the accepted line is printed on UART.
- Unsupported punctuation/non-ASCII or more than 75 characters reject the input
  on Return and leave the current sentence running. Backspace can correct either.
- A new accepted sentence cancels unfinished generation. Generation IDs suppress
  stale results; the canvas clears before the new generation's first point.

Supported characters, in addition to space, are:

```text
!"#'(),-.0123456789:;?ABCDEFGHIJKLMNOPRSTUVWYabcdefghijklmnopqrstuvwxyz
```

The renderer keeps a fixed 3-pixel/model-unit scale and continues on successive
landscape lines, preferring spaces and attended character changes at pen lifts.
There are three generously spaced lines. An extreme continuous stroke has a
hard edge wrap; this can split a glyph. Excess lines, extreme coordinate outliers
or ink outside the viewport produce UART feedback and diagnostic counters.
Outliers are rejected before arithmetic/conversion, and the next valid point
starts a new stroke connection. Text is never squeezed into one
small line. Stochastic unprimed generation can add trailing strokes, terminate
early or reach the original 40*length bound; text accuracy is not guaranteed.

## Execution and storage

CMSIS Stream dataflow reads a 512-byte interrupt UART ring every 5 ms, edits the
sentence, then sends one inference request at a time. The event thread owns
recurrent state and all five dense NPU calls. A completion acknowledgment releases
the next request after the prior result has been rendered. Sentence ownership,
layout, styling and animation are project nodes; the shared board layer supplies
UART/LCD hardware operations. The LCD node submits a VSYNC flip and defers its
two-event buffer-release wait until just before reuse, overlapping inference.
It copies only the prior stroke damage into the alternate buffer and cleans
only changed, aligned cache regions. Pen-up moves do not flip unchanged frames.
Replacement clears the complete canvas; the panel continues scanning normally.

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
records 64 warmed full-step samples, excluding UART and LCD time, for the clock
comparison; those are printed once on UART, without an LCD overlay.

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
ownership and declared cache regions, and exercise editing/backpressure:

```powershell
.venv/Scripts/python.exe tests/run_handwriting_tests.py --vcvars path/to/vcvars64.bat
# or use --cxx with an installed host C++17 compiler
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
