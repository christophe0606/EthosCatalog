# Reproducible memory-mode comparisons

The current numerical check is not repeatable. Performance measurements are
diagnostic until the first-token and synthesis checks pass in repeated runs.
The frame budget is 80 ms for 1920 samples at 24 kHz, including CPU operators,
state copies and PCM production. Check UART/prefill latency separately.

## Baseline snapshot

The ignored artifact
`model/artifacts/memory_modes/shared_512_25mhz_full_gelu/manifest.json`
records Release b-51, Shared_Sram, 512 Flow positions and 25 MHz flash/RAM.
All 27 copied files have been verified against their manifest hashes. It
includes the generated PTE/layer, network settings, startup assets, fixtures,
project/runtime/board sources and flash algorithm. Method cache hashes are
recorded; the much larger caches are not copied. The AXF is identified by
SHA-256 in NUMERICAL_ISOLATION.md, but is not part of this initial snapshot.
Rebuilding its saved sources is required if the original output was overwritten.
Future mode snapshots should also copy the matching AXF before the next build.

Before changing mode, preserve any newer source changes in a new snapshot.
Verify copied files before restoration, and restore only the files required
for the comparison while preserving unrelated user changes. Rebuild only
Phonon Release through CMSIS. A host snapshot does not restore target flash:
program the matching PTE/assets through CMSIS when changed, then check their
complete target CRCs before inference.

## Clock comparison

The fitted IS25WX01G flash is limited to 166 MHz (ISSI Rev A1, 2025-08-14,
pp25,82). The bounded board API `e8_flash_set_read_clock` accepts 25, 50 and
100 MHz with exact legal even AXI divisors. Its caller must run from internal
memory before any other flash master starts. It preserves protocol, dummy
cycles and RXDS settings; a successful return verifies divider readback,
not data integrity. Run the complete flash CRC at each tested clock.

HyperRAM identification does not encode the fitted speed or temperature
grade. The data sheet includes both 166 and 200 MHz grades. Use at most
100 MHz for the present bounded comparison. Verify both die CR0 values,
controller latency, full capacity and inference numerics after initialization.
Respect tCSM: ISSI Rev A1, 2024-10-28 p25 requires each transaction to finish
within 4 us for industrial/A1 or 1 us for A2. The pack HyperBus initializer
disables continuous transfer and prefetch, but this alone does not measure
the longest actual chip-select interval. Do not claim that changing the clock
proves or fixes refresh behavior.

## Dedicated_Sram comparison

Regenerate the complete PTE/layer from the same saved caches with only Vela's
memory mode changed. Preserve source, startup assets, quantization and fixtures.
Record actual generated PTE hash/size, then program and verify that exact PTE.
Dedicated_Sram needs a separate strong fast-scratch pointer and at least
393216 bytes of verified NPU-visible SRAM. The largest slow-scratch request
remains 1231360 bytes; application pools and stacks are additional allocations.
Record every pool's actual address and peak use. Do not count Vela's fast SRAM
estimate as the complete application RAM footprint.

Release b-59/d-60 passes all three complete flash CRCs (Dedicated PTE
B8BB566C, prefix 9D869927, BOS DC5D06F4) and full 64 MiB RAM readback at
100 MHz. Its first NPU command completes. Live command inspection confirms:

| Allocation | Address | Allocated bytes |
| --- | --- | ---: |
| Dedicated fast scratch, base2 | 0x02000000 | 393216 |
| Method metadata pool | 0x02060000 | 262144 |
| Slow temporary pool, base1 | 0x020A0000 | 1572864 |

The first backbone command requests 5152 slow bytes. After command setup,
U85 REGIONCFG is 0x13: region0 uses MEM_ATTR3, region1 MEM_ATTR0 and region2
MEM_ATTR1. QCONFIG is 2, MEM_ATTR0/1 are 0 and MEM_ATTR2/3 are 0x24. This
records actual SRAM backing for slow and fast scratch, distinct from Vela's
abstract memory-port assumptions. The typed driver gives NPU BASE 0x49042000.
Register meanings: U85 TRM 102685_0000_05_en, r0p0 Issue 05, pp36,56-57.

CMSIS build memory usage is 2228240 of 4194304 bytes in SRAM0, leaving
1966064 bytes there before any additional application allocations. Other
regions have their own code/data/stack/heap use. This is a benchmark footprint,
not a complete UART/audio/LCD application memory measurement.

Two uninterrupted complete frames take 18458 and 18461 ms, mean 18459.5 ms
for 80 ms audio (RTF 230.74375, FAIL). Backend intervals total 401 and 404 ms;
the remainder is 18057 ms in both frames. All 61 begin/end hooks pair with
no error. Raw NPU cycles are 139372813 and 139372797, overflow zero; actual
HG frequency remains unverified. Metadata/temporary peaks are 165992/1231360
bytes. Prefix copy is 351 ms, seven-token prefill 58059 ms and neural time to
the first frame 76868 ms, excluding initialization/UART/playback.

This mode also fails numerical acceptance: first-token hidden/KV maximum
errors are 0.168142885/1.42356992, and first-two-frame maxima are hidden
0.319567919, latent 3.18251085, EOS 3.93008041 and PCM 0.234893143. Values
remain finite. First-token inspection contains pauses around the first NPU
command; the prefill/frame timing pair does not. See the raw
model/dedicated_100mhz_result.json and model/benchmark.yml records.

The verified 28-file local snapshot dedicated_512_100mhz_b59 includes the
matched AXF, SHA-256
b95ba7a4df1ad8d0548117dbf38679c82d89b8d36199355c0f519bb6e6b98b1f.
The measured work outside delegates alone exceeds the deadline by over
225 times, so a larger NPU fast cache cannot rescue this implementation.

Compare the same first-token checks and short uninterrupted frames in each
mode. Exclude debugger-paused timing. Keep a failing numerical run labelled
as such even when it is faster. Reserve enough measured headroom for UART,
audio buffering and application state before considering the landscape LCD.

## Interpretation of raw NPU counters

Wall time remains the measured frame latency. The actual HG clock has not
been independently queried, so counter values are not reported as measured
milliseconds. A separate conservative bound is possible: E8 datasheet
ADTS0013 v1.2 (June 2026), p138 limits NPU-HG to 400 MHz. The U85 TRM
102685_0000_05_en, r0p0 Issue 05, pp110–112 describes the dedicated cycle
counter and its start/stop-event configuration. The harness counts from
NPU_ACTIVE to NPU_IDLE for each delegate and checks overflow.

At the maximum permitted 400 MHz, the Shared_Sram 100 MHz run's
139992845 raw cycles require at least 349.98 ms, before CPU work. This is a
derived lower bound for that observed command stream, not an actual HG-clock
measurement and not a proof about a redesigned implementation. The measured
complete-frame mean is 18483 ms against an 80 ms deadline. Numerical quality
remains unaccepted independently of this timing failure.

## State and CPU operator work

Inspection of the actual Shared_Sram CLI PTE finds the following aggregate
logical CPU-kernel output sizes per backbone call:

| Operator family | Output bytes |
| --- | ---: |
| select_copy | 37773312 |
| cat | 37797888 |
| expand_copy | 12718272 |
| slice_copy | 12582912 |
| view_copy | 12582912 |
| permute_copy | 6352896 |

These are tensor output sizes, not measured bus traffic or elapsed time.
Input reads, caller state copies, cache maintenance and synthesis add work.
They explain why 96.08% dense-MAC delegation alone does not predict whole-frame
performance. Dedicated fast SRAM changes the NPU cache allocation; it leaves
these CPU operators and planned tensor sizes unchanged. The Dedicated harness
also measures complete backend execute intervals, including backend CPU I/O
copies and NPU wait. Subtracting their sum from frame wall time estimates the
work outside delegates; 61 intervals at a 1 kHz tick have an aggregate rounding
bound of about 61 ms. Neither interval is a pure CPU or pure NPU measurement.

A future optimization can investigate per-layer state inputs/outputs and
in-place rolling updates to avoid whole-stack selection and reassembly.
Such a redesign needs new caches, prefix/fixture provenance, numerical checks
and complete-frame timing. The present evidence does not establish its speed.
