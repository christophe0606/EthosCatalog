# Phonon 31m English port checkpoint

Updated: 2026-10-21.
Status: q8 conversion complete; hardware feasibility FAILS in both Shared_Sram
configurations and Dedicated_Sram. This retained experimental port is excluded
from the catalog. The required realtime UART-to-audio application is not achieved.
Mandatory review is complete with no unresolved review blockers. The measured
firmware is b-59/d-60; final diagnostic execution is CMSIS Run r-62 with the
same b59 firmware. COM7 is closed. CMSIS Run intentionally remains active;
no programming/build operation is pending.

## Required outcome

Import [Gradium Phonon 31m English](https://huggingface.co/gradium/phonon-31m-en)
using its published q8 source, PyTorch, ExecuTorch and Vela. Keep constants in
OSPI. Receive English text over UART and play synthesized speech in realtime.
Each frame produces 1920 mono samples at 24 kHz, giving an 80 ms deadline for
the complete inference/state/PCM path. An animated waveform on the landscape
LCD is optional only after measured application memory and cycle headroom.

Use the Alif E8 DevKit. The fitted flash is 128 MiB; the programming partition
is deliberately bounded to its first 64 MiB. External HyperRAM is 64 MiB.
Select the main EthosCatalog solution in VSCode. FlashLoader is the programming
utility. No physical restart or unanswered user question is pending.

## Measured hardware status

| Vela mode | Flash / RAM clock | Complete frame samples | Mean / 80 ms budget | Result |
| --- | ---: | --- | ---: | --- |
| Shared_Sram, b-51/d-54 | 25 MHz / 25 MHz | 59345, 59345 ms | 741.8125 times | FAIL |
| Shared_Sram, b-55/d-56 | 100 MHz / 100 MHz | 18486, 18480 ms | 231.0375 times | FAIL |
| Dedicated_Sram, b-59/d-60 | 100 MHz / 100 MHz | 18458, 18461 ms | 230.74375 times | FAIL |

The two frame intervals in each completed row are uninterrupted. Timing
includes backbone and synthesize, CPU state copies, finite checks, PCM16
conversion/storage and the diagnostic hidden comparison. It excludes
initialization, UART normalization/tokenization, physical playback, logging and
latent/EOS/PCM reference comparisons. These are diagnostic failure measurements,
not accepted synthesis performance. Raw records are in model/benchmark.yml,
model/shared_25mhz_result.json, model/shared_100mhz_result.json and
model/dedicated_100mhz_result.json, with model/hardware_bringup.json for bring-up.

Dedicated backend intervals total 401/404 ms, with 18057 ms per frame outside
backend execution. All 61 begin/end hooks pair correctly; aggregate backend
tick rounding is bounded by about 61 ms. The backend includes CPU I/O copies
and NPU wait, so this is not a pure CPU/NPU split. The outside-backend work
alone exceeds the deadline by over 225 times. A larger NPU fast cache cannot
make this current implementation realtime. Dedicated prefix/prefill are
351/58059 ms and neural time to the first frame is 76868 ms. Raw NPU cycles
are 139372813/139372797, with 61 invocations and zero overflow per frame.

At 100 MHz, immutable prefix copy is 352 ms and the pinned seven-token text
prefill is 58078 ms. Neural time to the first complete frame is 76916 ms before
initialization/UART/playback. Each complete frame invokes 61 delegates.
Raw NPU cycles are 139992845 and 139992766, with no PMU overflow. The actual
HG clock has not been independently verified; wall time is the primary result.
MEMORY_MODES.md distinguishes a conservative cycle-derived lower bound from
an actual clock measurement.

Both clock configurations pass complete physical PTE/prefix/BOS CRCs, a full
64 MiB address-dependent RAM write/cache-clean/readback and all three method
loads after fresh initialization. RAM dies report CR0 0x8F2F8F2F, fixed initial
latency 7. The controller wait must be 7; a trial using 14 failed byte order.
The 100 MHz clocks are verified by AXI 400 MHz and both BAUDR values 4.
These tests do not certify arbitrary access patterns or synthesis correctness.
Do not try 200 MHz: fitted flash is limited to 166 MHz and the RAM speed and
temperature grade is not established. See model/hardware_bringup.json and
MEMORY_MODES.md.

## Numerical failure remains open

Global Release compilation is -O3 -ffast-math. Only the Portable GELU component
uses -fno-fast-math. This isolated change reduced first-token hidden/KV errors
from 4.61525488/8.19478226 to 0.000190377235/0.000855207443. A narrower
-fno-finite-math-only trial was rejected. A byte-identical repeat once diverged;
subsequent captures and both latest Shared runs repeat the small first-token
errors. Repeatability is not established.

Full-frame differences remain unaccepted. At 100 MHz the first two frames have
maximum absolute errors hidden 0.377230823, latent 1.67419875, EOS 4.01282406
and PCM 0.108738497. Bitwise finite checks pass. There has been no accepted
speech listening test or native upstream-runtime parity test. Do not infer
intelligibility or quality from finite values or dense-MAC coverage. Detailed
isolation, rejected trials and repeat evidence are in NUMERICAL_ISOLATION.md.

Dedicated b-59/d-60 also fails the first-token comparison: hidden error
0.168142885 and KV error 1.42356992, with layer-zero K/V errors already
1.13564205/0.216028303. First-two-frame maxima are hidden 0.319567919,
latent 3.18251085, EOS 3.93008041 and PCM 0.234893143. Values are finite.
First-token inspection includes pauses around the first NPU command; the
following prefill/frame timing pair is uninterrupted. Do not treat physical
memory gates as quality acceptance.

## Conversion and memory contract

The source snapshot is 52433c46975bfa13144c56204c9f4ee1c5b148aa. The q8 GGUF
contains 185 tensors and 41096035 elements, including the Mimi codec; the 31M
headline excludes the codec. Q8_0 transformer tensors and remaining BF16 tensors
are imported without q4. Selective a16w8 linear/convolution lowering retains
float attention, normalization and state. See CONVERSION.md, NOTICE.md and
LICENSES for validation, provenance and Apache-compatible notices. No GPL is used.

The PTE contains three methods: backbone (24 Ethos delegates), synthesize (37)
and CPU text_embedding (0). Delegates are subgraphs, not methods. Flow capacity
is 512 positions; reject or split oversized inputs. A smaller capacity requires
regenerating all affected graphs, caches, prefix and fixtures, not truncation.

Actual repository CLI PTEs, distinct from earlier staged converter exports:

- Shared_Sram: 44675472 bytes; SHA-256
  cf07d603198495055f45bdc6eae73d119823c0d196cff65e4bf84c849844fab7;
  CRC A5F23DC5.
- Dedicated_Sram: 44678800 bytes; SHA-256
  a2f0a57d8b2f7d628aeeea7f391c2070445b215e4398fed967b6161363b90dd5;
  CRC B8BB566C. Host preflight and full target CRC pass in b-59/d-60.

Immutable final-PTQ voice prefix: 125 tokens, 12582912 bytes at 0xC3000000,
CRC 9D869927. BOS: 2048 bytes at 0xC3C00000, CRC DC5D06F4. Copy prefix into
writable Flow state per utterance. Zero Mimi/convolution state, prefill text,
then supply BOS. Do not mutate OSPI. Recheck all three complete CRCs after
programming; unchanged asset hashes do not replace target readback.

The backbone planned arena is 38801424 bytes, synthesize 6716144 and embedding
2064. Sequential methods safely alias one 38801424-byte arena in HyperRAM;
copy needed outputs before reuse. Aligned caller inputs/state occupy 14694208
bytes separately. Measured full-synthesis peaks are 165992 bytes for method
metadata and 1231360 bytes for temporary scratch. Allocated SRAM pools are
256 KiB method and 1.5 MiB temporary. Dedicated mode adds 393216 bytes of
separate fast SRAM. Actual b59 addresses are fast 0x02000000, method
0x02060000 and slow temporary 0x020A0000; SRAM0 build use is 2228240 of
4194304 bytes. Full CRC/RAM gates pass at 100 MHz. See MEMORY_MODES.md for
live NPU routing. Actual application UART/audio/LCD memory is not measured.
This is a performance/numerical failure, not a demonstrated application memory
failure; do not add a memory-failure entry to root UNSUCCESSFUL.md.

Local ignored mode snapshots preserve generated/model/runtime/board inputs.
shared_512_25mhz_full_gelu contains 27 verified files but no matching AXF;
it needs rebuilding. shared_512_100mhz_b55 contains 28 verified files including
the matched b55 AXF. Dedicated preflight proves unchanged method caches,
startup assets and fixtures, bounded slow-region I/O for all 61 delegates and
unchanged CPU arenas. The dedicated_512_100mhz_b59 snapshot has 28 verified
files including the matched b59 AXF, SHA-256
b95ba7a4df1ad8d0548117dbf38679c82d89b8d36199355c0f519bb6e6b98b1f.

## Retained source and checks

The hardware cproject builds app_main.cpp, not the template CMSIS Stream graph.
app.py raises an explicit unimplemented-entry error. Gain/LCD support files
are unused template scaffolding; the copied spectrum UI is removed and the
hardware_params.cpp application initializer returns an explicit unsupported error.
No UART tokenizer, physical audio playback,
TTS Stream graph or LCD application has been implemented or claimed working.
A host tokenizer has 34 exact parity cases; this is not a firmware tokenizer.

The project remains catalog: false. Its explicit ci.mode: metadata_only validates
source structure, schemas, paths, linker and target selection while recording
generated files, binaries/layout and firmware builds as SKIP with a reason.
It does not certify firmware or quality. Catalog exclusion alone does not skip
builds. All 65 relevant host regression tests pass after review corrections;
metadata validation passes
with these explicit skips. The catalog has been refreshed and excludes Phonon.

Review corrections preserve explicit Alif platform license scope/full terms,
remove unused spectrum UI code, make the unused hardware initializer reject
construction, add missing source headers and sanitize machine-specific paths
in historical failed-export records. Single-method graph provenance now binds
both graph and weights; its changed-graph regression passes. No GPL code was added.

Normal host regeneration after descriptive status changes reproduces the exact
measured Dedicated PTE hash/CRC, unchanged method caches, assets and fixtures.
Generated labels now explicitly say feasibility failure. Canonical C/header
emitters also add license comments. These changes do not alter the running
b59 binary; its exact compiled source/binary snapshot remains historical evidence.

At final handoff, the d60 debugger became unresponsive. A restart timed out;
d61 loaded firmware successfully but still had no responsive DAP threads.
After stopping the stale session, normal CMSIS load_and_run r62 succeeds.
UART repeats all three expected CRCs and the initialized clock line, then
reports frame zero at 18494 ms with 61 NPU calls. CMSIS Run remains active
and owned COM7 is closed. The DAP failure's cause is unresolved; do not infer
a proven firmware hang or long-run stability from this handoff check. See the
post_review_runtime_handoff record in model/dedicated_100mhz_result.json.

## Completion and resumption

All bounded hardware trials fail. Apply the planner's benchmark-failure branch:
the current implementation cannot meet the required realtime behavior and
numerical acceptance remains open. Retain the excluded project and review the
evidence/shared tooling. Do not present a slow or numerically unaccepted
UART/audio demo as successful. This conclusion concerns the tested implementation;
it is not a proof about every possible redesign for this board.

The mandatory review checked licenses, headers, source/metadata consistency,
shared model-generation/flash/CI changes and failure classification. Findings
were corrected and the final diagnostic Release is running through CMSIS. Future
resumption requires a materially different state/operator/data-movement design,
repeated numerical acceptance and new complete-path timing before application
coding. Optional LCD work remains deferred.

Preserve unrelated existing edits. Board/build/debug/serial and target manuals
use CMSIS Developer Assistant tools only. The Phonon cproject references its own generated `ai_layer/`. Switching target
sets preserves all project layers.
