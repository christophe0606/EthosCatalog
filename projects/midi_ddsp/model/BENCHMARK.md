# MIDI-DDSP: measured Ethos-U85 learned schedule

On the Alif E8 DevKit, the complete 1,000-frame learned schedule took **2.859 s**
on its first successful fresh firmware launch. Three subsequent instrument-0
schedules had a **2.838514 s median**. All 13 instrument identities completed;
the 15-run sweep ranged from **2.833945 to 2.838542 s**. Each block represents
four seconds at 16 kHz. These are measured complete schedule times, including
CPU caller work and stage preparation, rather than cell timing estimates.

This establishes neural throughput for the synthetic benchmark score. Audio
synthesis, convolution reverb, SAI output, playback overlap, buffering and any
LCD work are not included. The largest observed neural time leaves 1.141 s of
the four-second budget; that is not a measured voice capacity.

## Build, clocks and actual work

- Target: `DevKit-E8@MIDI_DDSP_Release`, AC6 6.24.0, `-O3 -ffast-math`.
- M55 HP and AXI service clocks: 400 MHz; U85 timing uses its 400 MHz PMU clock.
  Guarded OSPI driver configuration/readback: 100 MHz.
- Successful fresh launch: CMSIS b-50/d-51. Sweep: b-52/d-53, same-ELF restart.
- Each block performs 3,076 methods and 4,424 actual U85 invocations. Calls are
  `[4,4,4,63,1000,1000,1000,1]` in the method order below. Precondition packs
  16 independent rows, with 1,000 valid rows and eight discarded padding rows.
- Every successful method had CYCLE equal to NPU_ACTIVE, with zero overflow.
  Complete schedules were uninterrupted and all outputs passed finite checks.
- The synthetic score uses notes `48+ID,55+ID,rest,60+ID`, each 250 frames, and
  the documented LCG seed `9100+ID`. All 13 identities provide timing coverage;
  this score does not establish quality in each instrument's actual demo register.

The first successful schedule's detailed timing is below. Method wall time
excludes stage preparation, which is included in the complete block. NPU time
is contained within method wall time. Caller time outside methods was 417.725 ms;
it includes preparation, embeddings, packing, state/context/output copies,
top-p sampling, finite checks, statistics and profiling overhead.

| Method | Calls | Method wall ms | U85 ms | Preparation ms |
|---|---:|---:|---:|---:|
| Expression forward | 4 | 0.880 | 0.189 | 8.678 |
| Expression backward | 4 | 0.852 | 0.188 | 8.619 |
| Expression step | 4 | 4.537 | 0.397 | 20.908 |
| Precondition, batch 16 | 63 | 424.503 | 35.011 | 59.707 |
| Context forward | 1,000 | 323.373 | 154.696 | 21.902 |
| Context backward | 1,000 | 321.266 | 156.286 | 21.753 |
| Pitch step | 1,000 | 945.531 | 434.026 | 51.091 |
| Harmonic | 1 | 420.316 | 78.584 | 30.305 |

Total first-schedule U85 PMU time was 859.377 ms (343,750,884 cycles).
First-use held calls and three warm repetitions are saved separately in
`benchmark_batch16_held_timings.json` and summarized in
`benchmark_batch16_summary.json`. The first precondition call took 6.735 ms,
its warm median 6.730 ms; harmonic took 420.247 ms first use and 420.205 ms warm
median. These are first use after staging and CRC, not a power-on cold-cache
experiment. The full schedules also follow held calls; caches were not forcibly
invalidated between complete schedules.

## Memory, storage and lifetimes

The linked benchmark uses 3,721,216 bytes in SRAM0 at `0x02000000` and
3,355,620 bytes in SRAM1 at `0x02400000`, each a separate 4 MiB bank. Runtime
ten-point simultaneous write/read checks and actual U85 access passed. Public
E8 documentation establishes capacity/reachability but not those addresses;
the project-owned SRAM1 correction follows the E8 pack's wizard default and
live hardware tests. Imported headers were not changed.

The runtime method pool is 393,216 bytes; observed peak is 190,180 bytes.
Temporary pool capacity is 1,310,720 bytes; observed maximum is 1,216,000 bytes.
Precondition itself peaks at 190,180/49,152 bytes. These pool figures exclude
the separate 3,072,000-byte harmonic planned arena, inside a 3,328,000-byte phase
union. The union's learned features and bidirectional contexts die only after
the full harmonic input has been packed. The pinned runtime resets temporary
allocation after each delegate; individual scratch demands are not summed.

SRAM1 also holds the 1,536,000-byte harmonic input and 504,000-byte retained
control output. The 4,900-byte small-method input has explicit SRAM placement.
Linked DTCM application RW is 259,512 bytes, plus a 3,296-byte nonsecure region,
131,072 bytes of heaps and a 32,768-byte stack. The benchmark contains 15 result
slots and detailed profiling; it is not an audio firmware memory measurement.
See `benchmark_batch16_memory.json` and `benchmark_batch16_sweep_memory.json`
for the build-artefact tool output. The final sweep ELF occupies 534,000 bytes
of MRAM, with text 523,376, data 10,624 and BSS 7,492,876 bytes.

Each recurrent stage's complete backend segments are staged into bounded SRAM,
CRC-checked, cleaned for NPU visibility, retained while its method is alive,
then released before reuse. The pitch stage borrows dead feature storage;
harmonic delegates remain in OSPI. SRAM route attribute 1 is `0x20`; direct
OSPI attributes 2/3 are the verified `0x24`. A fresh bounded module caches all
47,136 bytes of PTE CPU constants: zero bypasses or segment CRC failures in the
successful runs. This does not mean the separate CPU embedding/IR asset is
copied wholesale. No imported runtime/pack files were modified.

## Numerical and startup limits

All 717 independent target quantization edge cases passed, including vector
NaN/range guards, ties, tails and saturation. Six CPU normalization overrides
had zero fallbacks, including the batched row broadcasts. Fast MVE quantization
and the measured faster native dequantization were used.

All eight held maximum errors match the preceding batch-1 board candidate.
Precondition maximum is 0.000207782. Harmonic maximum **0.028621495** exceeds
the unchanged provisional **0.0119089603** gate; harmonic NRMSE is 0.164406%.
Instrument-0 coupled pitch MAE is 0.028380014 semitones and selected raw maximum
0.016355157, matching the preceding board schedule. Consequently benchmark
`test_result` remains **FAIL**, even though the neural timing meets four seconds.
No numerical gate was relaxed to obtain a pass.

One b-52/d-53 launch returned ExecuTorch InvalidProgram (35) on the first held
expression, before the sweep. The same ELF's restart completed the whole sweep.
Two earlier launches had the same symptom. The precise origin is unresolved;
a successful diagnostic restart does not locate the earlier failure. The failed
launch is saved separately in `benchmark_batch16_startup_failure.json` and is
excluded from performance samples. Reliable startup, actual audio quality,
demo-register coverage and seams remain acceptance work.

## Final integrated music demo

The later [b-91/d-92 Release run](stream_final_b91.json) completed the included
104-second monophonic score with all thirteen instruments and its final reverb
tail. Its maximum overlapped neural window was 3.9177995 seconds per four seconds
of controls; 6,688 audio blocks were queued and completed with zero active queue
starvation or neural/audio deadline misses. Both codec channels read back code49
(-8 dB), 12 dB above the original setting. The report binds the final source and
ELF hashes and includes startup, DSP probe and finite-drain counters.

This result applies to the included aligned score and bounded admission policy.
It does not change the failed numerical gates above or guarantee arbitrary MIDI
seams, ranges or polyphony. The earlier startup failures remain historical;
project startup now clears the pending NVIC line before enabling it. The final
fresh run completed without held-method or schedule runtime errors.

## Provenance and reproduction

Current programmed assets, all fully CRC-verified on board:

| Asset | Bytes | CRC32 | SHA-256 |
|---|---:|---|---|
| PTE | 6,221,424 | `de624a9e` | `e1cf5cc94363f68cc76dec54347d75472d36b7e200b92be33e59c5e0355a1d52` |
| CPU weights | 2,536,448 | `e39d5051` | `19ed82d68c4c68eaceab7c7471e25cdd94cf80eb520bae427316865418110523` |
| Held fixtures | 2,078,208 | `da5db206` | `af6a0d4c75e2d19603122d232940b14552bc35f8fa2ca5099031d1a1e12d77f2` |

Parent generation/inspection and storage checks preserve all seven unchanged
methods' delegate bytes, CPU constants and execution plans. Batch 16 retains
the original precondition weights/qparams and independent per-row equations.
Native host batched GEMM has a separately recorded tight parity-gate failure
up to 0.00041556; host rowwise summation independence is exact. Board held and
full trace checks are reported above rather than assuming host bit identity.

Raw evidence is in `benchmark_batch16_first.json`, `benchmark_batch16_sweep.json`
and `benchmark_batch16_sweep_checks.json`. The repository schema accepts the
seven actual measurement records in `benchmark.yml`. Recreate the compact
summary and two current-candidate schema records without accessing the board:

```text
.venv/Scripts/python.exe projects/midi_ddsp/model/summarize_board_benchmark.py --update-benchmark
.venv/Scripts/python.exe scripts/validate_benchmarks.py projects/midi_ddsp/model/benchmark.yml
```

Firmware source retains the held checks and complete scheduler. Use CMSIS tools
for Release build/load/debug; prepare OSPI with ProgramModel and program the
standard `out/midi_ddsp-model.cbuild-run.yml` after any coordinated asset update.
Do not clean the solution routinely: its clean task removes the shared `tmp`
tree. The target was left running with matching Release symbols and no benchmark
breakpoints after capture.
