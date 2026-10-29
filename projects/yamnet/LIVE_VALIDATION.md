# YAMNet live validation

This report preserves the original b-91/d-92 measurements. The subsequent
separation of CPU postprocessing from inference and display is recorded in
[POSTPROCESSING_VALIDATION.md](POSTPROCESSING_VALIDATION.md), with the current
firmware hash and fresh board results.

Final Release build **b-91**, loaded with matching Release symbols in **d-92**,
passed on Alif DevKit-E8 M55_HP / Ethos-U85-256 on 2026-10-29. The final run was
left running without debugger pauses. The model executes directly from OSPI at
100 MHz. No full PTE copy exists in SRAM.

Firmware SHA256: `b8a6d3bfb0dfefa9f6cdaaff48e11bea769f185b0f9550fd3d40612b2072a034`.
PTE SHA256: `bd5c2b892839f8d42467f284a448cbc35a2510a4930959ae1dd5e4987deb86b4`.
[Machine-readable provenance](model/demo_live_provenance.json) binds the image,
model, clocks, startup and live figures. [Raw live UART](model/demo_live_uart.txt)
contains successive health reports from this uninterrupted run.

## Startup

The entire PTE passes the generated CRC before runtime initialization. The
firmware frontend checks both an isolated frame and three streaming windows
starting at samples 0, 160 and 320 against independent TensorFlow outputs.
It also checks every raw sample index and rejects nonfinite FFT/log-mel results.
Measured maximum absolute log-mel error was **0.000000477**, below 0.001.

All 40 retained synthetic classification fixtures and ten warmed forwards pass:
PTQ top-1 agrees on 40/40, original TensorFlow agrees on 39/40, and its top class
is in the board top five on 40/40. Every forward completes 14 NPU calls with
active/total PMU cycles equal, nonzero cycles, and no overflow. Finite independent
sigmoid scores stay within [0,1]. Strict score parity at 0.005 remains separately
**FAIL (-405)**, maximum PTQ score difference 0.064208984. This is not labelled
AudioSet accuracy or calibrated probability validation. Startup takes about
75 seconds and the LCD explains validation is in progress.

## Uninterrupted live run

| Counter | First health line | Tenth result | Twentieth result |
| --- | ---: | ---: | ---: |
| Captured 160-sample blocks | 254 | 1679 | 3262 |
| Valid log-mel frames | 252 | 1677 | 3260 |
| Completed inference / LCD presents | 1 / 1 | 10 / 10 | 20 / 20 |
| Deliberately coalesced windows | 2 | 22 | 45 |
| Audio deadline misses | 0 | 0 | 0 |
| Capture drops / overflows | 0 / 0 | 0 / 0 | 0 / 0 |
| Request / pipeline errors | 0 / 0 | 0 / 0 | 0 / 0 |
| Full forward, ms | 1548.427 | 1548.387 | 1547.769 |
| NPU subtotal, ms | 60.736 | 60.737 | 60.737 |

The final proof spans over 32 seconds of continuous live capture after startup.
Inference preemption and display work explain overhead above the separate
1482.160 ms standalone benchmark. Exactly one in-flight request plus one mutable
latest-patch slot bounds work. Coalescing is intentional and can miss brief events;
it is distinct from dropped capture data or failed event publication.

## Memory and stack

CMSIS build-artefact tools report MRAM load 1,857,296 bytes, leaving 239,856 bytes
in the 2 MiB application window. SRAM0 uses 3,108,880 bytes including padding,
leaving 1,085,424 of its 4,194,304 bytes. This includes the 1 MiB method pool,
512 KiB temporary pool and both landscape framebuffers (1,536,000 bytes).
DTCM RW is 258,632 bytes, including retained selftest state and RTX memory.
C-library/application heaps reserve 98,304 / 32,768 bytes; the main stack reserves
32,768 bytes. Heap high-water is not instrumented. Actual complete model, graph,
FFT scratch and large publisher-node allocations all succeeded.

Inference pool peaks remain 829,016 / 298,752 bytes. RTX stack watermarking is
enabled. After twenty live results, the audio thread retained **31,816 / 32,768**
bytes and the inference/event thread retained **14,104 / 16,384** bytes. Stack
checking remains enabled. The low-rate health line reports these figures for
continued monitoring.

## Host and integration checks

- Native MSVC C++17 tests pass against production AudioWindow, SendToNetwork,
  YAMNet and EventLCD wrappers. They exercise exact ramp alignment, immutable
  in-flight input, one request, pending-window replacement, acknowledgment,
  sorted results and LCD routing through real CMSIS Stream events.
- Graph test passes: only five audio nodes are in the 10 ms dataflow; network
  and LCD are event-only. FIFO storage is 2,240 bytes.
- Graph generation succeeds from repository root and project directory, with
  one consistently colored network node and distinct hardware nodes.
- Network and benchmark YAML schemas pass. Catalog generation and its test pass.
- Host rendering of the actual UI checks startup and five longest labels in
  landscape orientation. This is visual layout QA, not a photograph of the LCD.
  Actual board LCD present calls advance without errors.

Early integration builds exposed default RTE example shims, a missing selector
member, unsupported raw-pointer event data, missing PMU declarations and a missing
PCM conversion source. The project now uses shared RTE shims and explicit source
ownership; event transport encodes the immutable pointer in a uint64_t token.
Those were application integration issues. Compiler log parsing omitted C++
diagnostics; native wrapper compilation and source inspection resolved them.
No imported pack/library was edited. A permission exception to read one build
log was later granted but was not needed or used.

The earlier diagnostic pause used for live inspection can induce a microphone
sequence gap; the final d-92 proof uses permanent sparse UART health output and
was never paused after startup continuation. The firmware fails capture gaps
rather than silently mixing windows. Keep final validation uninterrupted.

## Extended observation before handoff



The same uninterrupted d-92 run continued to **80 inference/LCD results** and

12,755 captured blocks (over 127 seconds of audio), with 12,753 valid feature

frames and 183 deliberately coalesced windows. Capture drops, overflows,

deadline misses, request errors and pipeline errors remained zero. Live forward

samples stayed near 1.548–1.550 seconds and NPU time near60.736 ms. Minimum

reported unused audio/event stack space was **31,792 / 14,104 bytes**. Pool peaks

were unchanged. All health lines through result80 are retained in the raw UART.

COM5 was closed after collection and the target remained running.

## Final review and handoff

Independent review passed with no blocking source, model, license, memory or
catalog findings. The generated local `.clangd` is excluded from version control.
No source or firmware changes followed the b-91/d-92 validation.

The same unpaused run reached **270 inference/LCD updates**, 42,805 captured
blocks and 42,803 valid feature frames (over seven minutes of live audio).
All capture, overflow, deadline, request and pipeline error counters remained
zero; 619 windows were deliberately coalesced. Full forward/NPU time at the
last health report was 1548.322 / 60.736 ms. Stack and inference pool peaks
were unchanged. The two final health reports span 1,582 audio blocks and ten
displayed results, about 1.58 seconds per result (0.63 results/s), including
presentation and request handoff. [Final UART](model/final_live_uart.txt) and
[verification metadata](model/final_verification.json) retain the observations.
Firmware and PTE hashes were rechecked, the debugger uses the matching Release
ELF, breakpoints are empty, the serial port is closed, and the target is running.

