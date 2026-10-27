# Experimental FARGAN board benchmark

FARGAN executes on Ethos-U85 but **fails the real-time budget and both vocoder
method numerical checks** in the measured configuration. This cproject remains
an experimental benchmark, not a validated speaker demo. No speed/quality
optimization was attempted after obtaining these results.

The board is Alif DevKit-E8, Cortex-M55 HP/Ethos-U85-256 at 400 MHz, OSPI1
25 MHz and SRAM0 activations. Release uses Arm Compiler 6.24.0 with
`-O3 -ffast-math`. Build `b-27` and uninterrupted final run `r-28` produced
the evidence on 2026-10-27. All six methods are loaded persistently.

| Method | NPU median (ms) | Execute median (ms) | Max error vs quantized host | Allowed error | Result |
| --- | ---: | ---: | ---: | ---: | --- |
| prosody_pre | 0.419900 | 1.451289 | 0.000134170 | 0.000135132 | PASS |
| prosody_gru | 2.254930 | 2.657840 | 0.000100434 | 0.000134939 | PASS |
| prosody_head | 0.710960 | 1.528429 | 0.000809669 | 0.001619695 | PASS |
| acoustic | 75.363606 | 77.490565 | 0.001083851 | 0.001084467 | PASS |
| fargan_init | 91.087505 | 91.843640 | 0.007872820 | 0.000184089 | **FAIL** |
| fargan_frame | 107.571273 | 108.211584 | 0.037712593 | 0.002012338 | **FAIL** |

Each method has one warmup and ten samples. Execute includes CPU boundary
operators and the complete delegated method; caller copies, embedding/index
construction, de-emphasis and PCM conversion are outside this isolated timer.
Every call reports one Ethos delegate invocation, equal CYCLE/NPU_ACTIVE
counters and zero overflow. The two failed tolerances are retained without
relaxation. The frame error is not a real-time assertion or a quality score.

The complete scheduled vocoder frame includes pitch embedding lookup,
previous-period index construction, execute, 964-value recurrent state copy,
de-emphasis, clipping and PCM16 rounding. Across 83 consecutive calls in the
first timed 81-frame synthesis it took median **108.257958 ms**, minimum
108.248168 ms and maximum 108.260215 ms. Its deadline is **10 ms**, so the
median exceeds the budget by **10.826 times**. Initialization adds another
91.84 ms per independent synthesis. These measurements are already a decisive
real-time failure before text processing, LCD, I2S or other application work.

| Input frames | Output samples | Output duration (ms) | NPU sum (ms) | Prosody-to-PCM median (ms) | Real-time factor |
| --- | ---: | ---: | ---: | ---: | ---: |
| 2 | 480 | 30 | 602.376860 | 611.275203 | 20.375840 |
| 19 | 3,200 | 200 | 2,469.422163 | 2,502.786603 | 12.513933 |
| 81 | 13,120 | 820 | 9,431.635225 | 9,562.321145 | 11.661367 |

Each complete schedule has one warmup and three samples. It includes OSPI
embeddings, prosody input assembly, the exact paired bidirectional GRU scan,
head, acoustic tiles with 34-frame halos, pitch normalization, initializer,
N+2 complete recurrent vocoder frames, de-emphasis, the source's 80-sample
initial delay removal and final 80-sample flush trimming, and PCM16 output.
N input frames therefore produce (N+1)*160 samples at 16 kHz. It excludes
text/dictionary processing, UART, LCD and I2S playback. Printing and reference
comparison are outside the timer. The 64-bit elapsed timer resolves system
counter wraps against the RTOS tick timebase.

The full schedules made 10, 44 and 174 NPU invocations respectively, matching
`N + 3*ceil(N/32) + 1 + (N+2)`. Their shared feature errors were 0.001083910,
0.001755612 and 0.002174795, all below the 0.01 feature tolerance. The overall
result remains **FAIL** because both vocoder methods failed their independent
strict reference checks. The output is finite and no PCM16 sample clipped.

| Input frames | PCM maximum error | PCM RMSE | Reference RMS | PCM SNR (dB) |
| --- | ---: | ---: | ---: | ---: |
| 2 | 0.004793361 | 0.001560123 | 0.007335512 | 13.445434 |
| 19 | 0.053781919 | 0.006674478 | 0.032609383 | 13.778505 |
| 81 | 0.169768542 | 0.029899406 | 0.054967439 | 5.288859 |

These PCM comparisons use independently generated float source-model waveforms
for the same synthetic phone/stress/word inputs, including pitch correction,
state feedback and output flushing. They include errors from board feature
inference and vocoder lowering. They are **different tests** from the earlier
host PTQ 173-frame, 13.109 dB closed-loop comparison. Neither result establishes
perceptual quality, and there has been no FARGAN speaker listening validation.

The initialization discrepancy is approximately **172 output quantization
steps**, and the frame discrepancy is **75 steps**. Neither is explained by
final output rounding. Initialization keeps the 256-value pitch history zero
for every subframe and discards each returned pitch update, so all valid
pitch indices gather zero values. This rules out merely choosing different
valid dynamic pitch indices as the explanation for its error. A bounded
host fixture/cache invariance check is recorded separately in
`benchmark_diagnosis.json`. It reproduces both saved fixture outputs exactly
from their PT2/SafeTensors caches; initializer results are bit-identical with
all indices forced to 0 or 255. The exact internal lowering discrepancy remains
unresolved. Frame index sensitivity alone does not localize the frame error.

Memory fits; it is not the measured blocker:

- PTE 2,608,624 bytes, six Ethos delegates, verified CRC32 `0xa96342cd`.
- Source embedding/norm assets 201,120 + 406,524 bytes, both CRC checks pass.
  Total PTE/assets is 3,216,268 bytes; CMSIS flash programmed 3,223,552 bytes
  including page alignment.
- Read-only Ethos regions: 11,968 / 63,008 / 19,136 / 395,088 / 913,120 /
  1,068,864 bytes, total 2,471,184 bytes, already contained in the PTE.
- Ethos arenas: 15,872 / 1,664 / 12,672 / 93,216 / 8,208 / 10,240 bytes.
  The maximum is 93,216 bytes and is already contained in the temporary pool.
- Observed method/temp pool peaks: 184,852 / 93,216 bytes. Reserved pools are
  1,048,576 / 524,288 bytes. They were not shrunk as an optional optimization.
- SRAM0 LOAD usage: 2,078,752 / 4,194,304 bytes. MRAM LOAD usage: 578,960 bytes.
  DTCM regions include ordinary data 91,668 bytes, RTX region 3,296 bytes,
  configured heaps 131,072 bytes and main stack 32,768 bytes. The 32 KiB
  application-thread stack is included in ordinary data. There are no LCD,
  I2S playback or text frontend/dictionary buffers in this benchmark image.

The model was programmed through CMSIS after ProgramModel `b-25`/`d-26`
reached `app_stage=5`, `model_programming_ready=1`, `app_error=0`,
`app_service_error=0`. The first build found a comma in the generated model
group name that CMake rejected. The generator and layer group name were
sanitized without changing the PTE, caches or model hashes. The final raw
capture begins at the last startup of `load_and_run`; the load/run handoff's
earlier partial startup is excluded. No debugger stop or reset occurred during
the saved final samples.

[benchmark.yml](benchmark.yml) records the 81-frame synthesis with FAIL.
Separate method, shorter-sequence and complete-frame YAML records preserve
their own shapes and scopes. All 12 FARGAN YAML files pass the repository
benchmark schema. Raw evidence is in [benchmark_uart.txt](benchmark_uart.txt),
[benchmark_details.json](benchmark_details.json), and
[benchmark_memory.txt](benchmark_memory.txt).

To regenerate summaries from the saved UART capture and matching ELF:

```powershell
.venv/Scripts/python.exe projects/rvtts_fargan/model/record_benchmark.py --recorded-at 2026-10-27T11:30:00Z --build-job b-27 --execution-job r-28
.venv/Scripts/python.exe scripts/validate_benchmarks.py
```

PTE SHA256:
`61ff0e022412af2507cf7ad085150affda951d18487c7c80064568b02c0e77c7`.
Firmware SHA256:
`758f2ecf12310785377c966bf7c2754965202f13d40ab87d19dbd8115989ad75`.

The target remains running in the benchmark's idle thread. Restoring the LPC
listening demo requires stopping CMSIS Run and programming its PTE again:
both variants use the model partition starting at `0xC0000000`.
