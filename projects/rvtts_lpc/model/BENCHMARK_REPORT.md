# rvTTS LPC on Alif DevKit-E8

This report preserves the feature-only benchmark. The subsequent complete
UART/I2S/LCD Release demo and its real-time measurements are documented in
[LIVE_VALIDATION.md](../LIVE_VALIDATION.md).

Measured on 2026-10-27 with Arm Compiler 6.24.0, Release `-O3 -ffast-math`,
Cortex-M55 HP and Ethos-U85-256 at 400 MHz, OSPI1 at 25 MHz, writable tensors
in SRAM0. Each case has one warmup and ten samples. No Helium kernel rewrite,
clock increase or optional optimization was applied.

The canonical [benchmark.yml](benchmark.yml) records the complete 157-frame
neural feature schedule. The other `benchmark_*.yml` files each have their own
input/output shapes, so method and shorter-sequence records remain valid under
the repository benchmark schema. All seven files pass `validate_benchmarks.py`.
[benchmark_uart.txt](benchmark_uart.txt) is the final uninterrupted capture;
[benchmark_details.json](benchmark_details.json) contains the parsed samples.

| Method | NPU median (ms) | Execute median (ms) | Weights region (bytes) | Arena (bytes) |
| --- | ---: | ---: | ---: | ---: |
| prosody_pre, 32 frames | 0.419908 | 1.437355 | 11,968 | 15,872 |
| prosody_gru, paired scan step | 2.254930 | 2.643565 | 63,008 | 1,664 |
| prosody_head, 32 frames | 0.710963 | 1.513638 | 19,136 | 12,672 |
| acoustic, 100-frame input / 32 center outputs | 75.354168 | 77.481205 | 395,088 | 93,216 |

Execute includes ExecuTorch dispatch, CPU float-boundary quantize/dequantize,
and NPU waiting. It excludes caller input/output copies and reference checking.
The PMU reports one invocation per method and zero overflows. The CYCLE and
NPU_ACTIVE event totals agree for every invocation.

| Frames | Equivalent mono 16 kHz audio (ms) | NPU sum (ms) | Complete feature schedule (ms) | Feature-only RTF | NPU calls |
| --- | ---: | ---: | ---: | ---: | ---: |
| 2 | 20 | 80.994891 | 86.280303 | 4.314015 | 5 |
| 81 | 810 | 412.104258 | 483.671721 | 0.597126 | 90 |
| 157 | 1,570 | 736.449000 | 872.122795 | 0.555492 | 172 |

One feature frame represents 160 eventual PCM samples. The table measures
feature generation, not generated or played audio. Complete feature schedule
timing includes OSPI embedding lookup, feature construction, both GRU directions,
state storage, head output, 34-frame acoustic halos, output copies and finite
checks. It excludes text/dictionary processing, the LPC vocoder, PCM conversion,
UART, LCD and I2S playback. Reference comparisons and UART printing are outside
the timed interval. The 157-frame headroom was approximately 698 ms for these
excluded stages; the later complete demo result is in
[LIVE_VALIDATION.md](../LIVE_VALIDATION.md).

The two-frame case pays a whole acoustic tile and exceeds its 20 ms audio
duration. A buffered utterance demo can absorb startup latency; this benchmark
does not establish a 10 ms streaming deadline, first-audio latency, absence of
audio underruns, or speech quality. The bidirectional prosody stage requires
the complete utterance before generating its final features.

The source reference uses the supplied int8 weights and float activations.
Maximum complete feature errors were 0.001083910, 0.002174795 and 0.002066970
for 2, 81 and 157 frames respectively, within the preselected 0.01 tolerance.
The kernel checks compare against the held-out quantized PyTorch fixtures.
Their tolerance is four output quantization steps plus 1e-6 for float arithmetic.
Initially, a strict four-step threshold rejected the pre method at 0.000134170
versus 0.000134132. Reading all 2,048 outputs through the debugger established
that this was the float rounding boundary (4.00112 LSB), not a shape or layout
error. The tiny float allowance also covers acoustic output arithmetic.

Memory and deployment evidence:

- Compiled PTE: 528,144 bytes, four Ethos delegates, CRC32 `0x7e22fd20`.
- OSPI source embedding/norm assets: 201,120 + 406,524 = 607,644 bytes.
  Their CRC checks pass before loading methods. Total PTE plus source assets is
  1,135,788 bytes; flash tool reported 1,142,784 programmed bytes after alignment.
- The four independent read-only Ethos regions total 489,200 bytes and are
  already inside the PTE. The largest arena is 93,216 bytes, already inside
  the ExecuTorch temporary pool; do not add these twice.
- All four methods are loaded persistently. Observed method pool peak is
  166,072 bytes and temporary pool peak is 93,216 bytes. Reserved pools remain
  524,288 + 131,072 bytes; no optional pool shrinking was performed.
- [benchmark_memory.txt](benchmark_memory.txt) records CMSIS build-artifact
  memory evidence. SRAM0 uses 1,076,528 / 4,194,304 bytes. The benchmark image
  has 433,824 bytes of text/data by ELF section totals; MRAM LOAD segments span
  433,840 bytes after alignment. DTCM execution regions include 86,220 bytes
  ordinary data, 3,296 bytes RTX region, 131,072 bytes configured heaps and
  32,768 bytes main stack. The application thread has a separate 32 KiB stack
  included in ordinary data. LCD, PCM playback and dictionary buffers are absent.
- `rvtts_engine.hpp` currently bounds an inference to 224 frames (2.24 s of
  eventual audio). The demo must explicitly handle input that exceeds this
  bound; this benchmark does not authorize truncation or independent GRU chunks.

Build job `b-7` produced the final Release image. CMSIS OSPI preparation reached
`app_stage=5`, `model_programming_ready=1`, `app_error=0`,
`app_service_error=0` before the model-only `flash` call. Final execution job
`r-9` used `load_and_run`. A previous debug launch showed duplicate startup
messages and restarted while an NPU command was active, then reported a busy
driver. The final captured startup completed all cases and `Test_result: PASS`
without a reset or breakpoint during its timed samples. The board remains
running in the benchmark's idle thread.

To regenerate summaries from the saved capture and corresponding ELF:

```powershell
.venv/Scripts/python.exe projects/rvtts_lpc/model/record_benchmark.py --recorded-at 2026-10-27T10:39:00Z --build-job b-7 --execution-job r-9
.venv/Scripts/python.exe scripts/validate_benchmarks.py
```

The saved firmware SHA256 is
`0b93ca98c4ffc20281e2016faf0335e2b325890d96f7b7d3b0bf2c96fe1cb9b4`.
The PTE SHA256 is
`763c3041689cf61a97c96ac3e66d6adae762621a61bfa8906e48857fdd9cbc11`.
