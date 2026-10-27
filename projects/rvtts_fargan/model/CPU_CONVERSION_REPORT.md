# Hybrid CPU FARGAN conversion

The current demo keeps the four shared prosody/acoustic ExecuTorch methods on
Ethos-U85. FARGAN's complete recurrent vocoder runs on Cortex-M55 using the
original synthesis order and LUT activations. The earlier six-method NPU
experiment and its failures remain in `BENCHMARK_REPORT.md`; they describe a
different firmware image.

`prepare_cpu.py` generates `fargan_cpu_weights.bin` from the preserved rvTTS
float matrix arrays, whose Opus provenance is recorded in
`official_provenance.json`. It does not use the older int8 FARGAN matrices.
Each output row is requantized to signed int16 with an independent float32
scale. Pitch embedding and biases remain float32. The 1,661,600-byte payload
(CRC32 `54ca3f64`) is stored at OSPI address `0xC1000000`. At startup its first
1,056,416 bytes are copied into SRAM0 and its final 605,184 bytes into DTCM.
The latter stores the three GRU input matrices, each evaluated four times per
160-sample frame. This frees 605,184 bytes of SRAM0 for the coordinating agent's
independent shared-PTE relocation experiment. Repeated matrix inference does
not stream weights from the 25 MHz OSPI interface.

Runtime matrix inputs are scaled dynamically into signed int16, using their
actual maximum absolute value. CMSIS-DSP 1.18.0
`arm_absmax_no_idx_f32` calculates that maximum; every input length is a
nonzero multiple of eight, avoiding the library's unpredicated tail loads.
Helium `vmlaldavaq_s16` performs exact signed 64-bit dot accumulation, reusing
one input vector across four output rows. Matrices are packed offline in order
`[four-row group, eight-column block, row within group, eight adjacent lanes]`.
This allows one sequential weight cursor. Final incomplete row groups remain
row-major, without padded rows. Every matrix input count is divisible by eight.
A scalar implementation consumes this same packed representation with the same
integer arithmetic for host verification and other CPU architectures. Float32
state, bias addition, LUT activations, gains and de-emphasis preserve the source
interfaces. No inline assembly is used.

The project-owned generated synthesis code preserves the original BSD function
bodies, except that the source's 10000-float skip scratch is bounded to its exact
688-float concatenation. The selected upstream source files remain unchanged.
This avoids an unnecessarily large event-thread stack.

Before board measurements, numerical acceptance was fixed at PCM SNR >=30 dB,
normalized PCM RMSE <=0.001 and maximum absolute difference <=0.01. Host
closed-loop synthesis across 173 frames/27,680 samples passes against an
independent build of the original scalar float C implementation:

| Comparison | Result |
|---|---:|
| Maximum normalized PCM difference | 0.000592803 |
| Normalized PCM RMSE | 0.0000648218 |
| Reference RMS | 0.0252187 |
| PCM SNR | 51.800 dB |

`cpu_equivalence.json`, `cpu_fixtures.npz` and `cpu_test_data.h` preserve these
results and all source/optimized fixture samples. The board startup self-test
runs the same entire recurrent sequence, applies the source gates unchanged,
and additionally checks maximum difference from the optimized host fixture
<=0.001. These numerical checks do not certify perceptual quality.

The four-method PTE and all OSPI assets are owned by this project. The method
pool is 192 KiB and temporary pool 128 KiB, based on measured shared-network
peaks of 166,072 and 93,216 bytes. The hybrid board build uses 4,036,708 bytes
of 4,194,304 bytes SRAM0, including both landscape framebuffers and CPU weights.

## Board baselines and packed-weight candidate

Board run r48 with the initial four-row kernel passes all 173 recurrent frames:
source maximum error 0.000597819, RMSE 0.0000636759, SNR 51.9549 dB; maximum
difference from the optimized host fixture is 0.0000875145. The full UART/I2S/LCD
demo runs with zero reported audio errors. Its Hello sentence has 92 feature
frames and 93 audio frames, but takes 2,676.435 ms to synthesize 930 ms audio
(RTF 2.878). CPU frame time is 22.624–22.647 ms and CPU priming takes 24.804 ms.
The complete numeric baseline and exact kernel source are preserved under
`cpu_benchmarks/r48_four_rows.json` and `cpu_benchmarks/r48_fargan_cpu.c.txt`.

The two-row candidate was measured as build b49 / run r50. It preserves the
same passing board accuracy. Hello CPU synthesis improves only 4.08%, to
2,064.543 ms including 23.836 ms priming, with individual frames taking
21.699–21.726 ms. Thus it remains slower than real time. Its exact source and
reported measurements are preserved in `cpu_benchmarks/r50_*`.

Standalone Clang compilation for Cortex-M55 shows the r48 four-row hot loop
spilling/reloading two accumulator halves each iteration, with five additional
register moves. The two-row hot loop has three vector loads, two long dot
operations and a low-overhead loop backedge, with no scalar stack access inside
that loop. `inspect_cpu_kernel.py` reproduces this isolated comparison; it is
not a firmware build and cannot establish actual speed. The host full recurrent
source comparison was rerun and has identical metrics to the table above.
The current candidate packs four rows into one sequential weight stream and
uses the split SRAM0/DTCM cache described above. Every int16 weight, row scale,
bias, input conversion and exact int64 dot sum is unchanged. `prepare_cpu.py`
checks that unpacking each full row group reproduces the original quantized
rows exactly. The complete 173-frame host PCM output is bitwise identical to
the saved two-row host baseline (maximum difference zero), and source accuracy
remains 51.800011 dB SNR with maximum error 0.000592802651. The standalone M55
hot loop now contains five vector loads,
four long dot operations and a low-overhead backedge, with no inner-loop stack
access. This is ten instructions for four rows, compared with six for two rows
in r50 and eighteen for four rows in r48. Board measurements must establish
the speed effect of packing and DTCM placement.

Startup identifies `rows=4 packed=1 dtcm=605184`. The CPU payload must be
reprogrammed at `0xC1000000` and AI-layer asset metadata regenerated to CRC
`54ca3f64` before loading this firmware. The shared neural PTE is unchanged;
the coordinating agent owns its separate runtime relocation experiment.
The implementation has passed board accuracy, complete demo timing and the
user's speaker listening check; see the board acceptance below.

## Board acceptance: packed Helium, DTCM weights and SRAM PTE

Release b-53 (confirmed current by b-56), run r-57, on 2026-10-27:
the 173-frame source check passes at 51.9549 dB SNR, RMSE 0.0000636759,
maximum error 0.000597819 and maximum difference from the quantized host
fixture 0.0000875145. The four Ethos method fixtures also pass.

The PTE is copied once from OSPI into an aligned 528,144-byte SRAM0 buffer,
verified against CRC 7e22fd20, and loaded there. NPU command-stream and weight
attributes are 0x20 (normal noncacheable memory on SRAM AXI ports), per
Arm Ethos-U85 TRM 102685 r0p0 pp.59,63. The existing cache-maintenance
callbacks and barriers transfer CPU-written buffers to the NPU. Original PTE
bytes and quantization remain unchanged; its OSPI Vela schedule is conservative
for SRAM. Both project AI layers were regenerated after the storage change.

| Phrase | Audio | Full synthesis | CPU vocoder | RTF |
|---|---:|---:|---:|---:|
| Hello. | 930 ms | 771.137 ms | 701.566 ms | 0.82918 |
| One two three. | 1230 ms | 1014.411 ms | 922.733 ms | 0.82472 |
| This is a working demo. | 1730 ms | 1421.190 ms | 1291.438 ms | 0.82150 |

Full synthesis includes text processing, the four shared Ethos feature stages,
pitch, CPU FARGAN, PCM conversion and publication. Later playback is excluded.
Individual complete CPU frames take 7.354-7.390 ms per 10 ms audio, with
8.36-8.40 ms priming. Hello CPU time is 3.07x faster than r-48, and complete
synthesis is 3.47x faster. Packing, DTCM placement and SRAM PTE caching were
measured together; these results do not attribute a separate speedup to each.

All 389 blocks / 62,240 samples from the three phrases complete without
underruns, deadline misses, event drops, UART errors or pipeline errors.
I2S callback intervals are 9.99920-10.00101 ms. LCD presentations reach 2266;
56 waveform updates coalesce while the inference event thread is occupied.
The display continues animating queued audio during playback. Immediate UART
echo, six-second status suppression while editing, Backspace/Delete visual
erase, single CRLF submission and explicit rejection of a 227-frame phrase
against the 224-frame limit pass. An additional edited Hello completes for a
total of 482 blocks / 77,120 samples, still with zero digital errors.

Build-artifact memory measurements are SRAM0 3,959,684 / 4,194,304 bytes,
ordinary DTCM 764,164 / 884,736 bytes, MRAM 878,064 bytes. DTCM heap/stack
reservations are separate. The CPU-only hot cache is at 0x2000de80;
no NPU tensor is placed in that cache.

The user completed the FARGAN speaker check on 2026-10-27 and reported:
"FARGAN sounds much better". This confirms listening acceptance of the CPU
FARGAN demo separately from the previous LPC acceptance. The final Release
remains running with COM5 released. The project is included in the catalog;
`benchmark.yml` records the accepted hybrid's full synthesis timing, while
`benchmark_npu_81.yml` preserves the earlier NPU-only catalog benchmark.
`cpu_board_validation.json` and `cpu_board_uart.txt` preserve the results.

## Reproduce

From the repository root:

```powershell
uv run projects/rvtts_fargan/model/prepare_cpu.py
uv run projects/rvtts_fargan/model/validate_cpu.py
uv run projects/rvtts_fargan/app.py
uv run scripts/create_ai_layer.py --project rvtts_fargan
```

Shared neural quantization is reproduced by `model/quantize_shared.py`, called
by `create_ai_layer.py --project rvtts_fargan --quantize`. The saved shared
graphs and weights match the LPC feature path; CPU vocoder requantization and
validation are separate explicit steps. Host compiler products remain under
`tmp/rvtts-reference-fargan/`.

Optimization guidance consulted:
`techniques-helium-matrix-kernel-layout`,
`patterns-cmsis-dsp-helium-migration` in the local Helium wiki, at
`.agents/skills/helium-wiki/wiki/techniques/techniques-helium-matrix-kernel-layout.md`
and `.agents/skills/helium-wiki/wiki/patterns/patterns-cmsis-dsp-helium-migration.md`.
Their row reuse
and bounded memory-access guidance is applied here; speed claims require
measured board results.
