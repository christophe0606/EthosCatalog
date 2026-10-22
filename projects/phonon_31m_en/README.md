---
description: Experimental UART text-to-speech port of Gradium Phonon 31m English q8.
category: speech
catalog: false
ci:
  mode: metadata_only
  reason: Experimental hardware feasibility port; generated model binaries are reproduced locally and inference is not accepted. See CHECKPOINT.md.
---

# Phonon 31m English q8

Experimental port excluded from the catalog: the tested implementation fails
realtime and numerical acceptance. The trained q8 model has been converted
through PyTorch, ExecuTorch and Vela. The real hardware benchmark replaces the
template gain application and builds in Release.
The complete 64 MiB external RAM test passed and all three runtime methods
loaded. ProgramModel is preparation firmware, not a TTS application.

The gain/LCD nodes are retained unused template scaffolding, excluded from
the hardware benchmark build. The copied spectrum UI is removed, and
`hardware_params.cpp` explicitly rejects application initialization. `app.py` reserves the
future CMSIS Stream entry point and stops with an explicit error until the
UART-to-audio application is implemented. It does not generate a gain graph
under this TTS project's name.

CI validates the retained source structure, configuration and benchmark metadata.
The explicit `metadata_only` policy skips generated Stream files, binary presence/layout and
firmware builds on fresh runners; it does not certify this unfinished port.
Reproduce the model caches and startup assets locally using CONVERSION.md before
building. Remove this CI policy when the project is accepted and its model
reproduction is integrated into automated builds.

Generate its project-local layer with
`uv run scripts/create_ai_layer.py --project phonon_31m_en`. The cproject uses
`projects/phonon_31m_en/ai_layer/ai_layer.clayer.yml`; other applications retain
their own layers when Phonon is generated or built.

The retained `EthosGain` wrapper is reusable across compatible signal-processing
models, as in the MRAM/OSPI templates. `LCD` and `SendToLCD` retain the template's
application-specific spectrum/touch/statistics policies and remain local UI
wrappers; they need adaptation for a TTS waveform display. None participates in
the Phonon benchmark, and no new TTS Stream node or graph is implemented.

Source: [Gradium Phonon 31m English](https://huggingface.co/gradium/phonon-31m-en),
revision `52433c46975bfa13144c56204c9f4ee1c5b148aa`. The q8 GGUF is the
production source; q4 is not used. Linear and convolution operations use int8
weights with int16 activations; attention, normalization and persistent state
remain float32 after calibration rejected a global int8 conversion.

The generated PTE has **3 methods and 61 Ethos delegates**. Delegates are NPU
subgraphs within methods, not separate PTE methods:

| Method | Purpose | Ethos delegates |
| --- | --- | ---: |
| `backbone` | Text/audio conditioning and Flow transformer step | 24 |
| `synthesize` | Flow latent generation and Mimi audio decoding | 37 |
| `text_embedding` | Token embedding lookup on the CPU | 0 |

The repository-generated Shared_Sram PTE is 44,675,472 bytes. The current
Dedicated_Sram comparison export is 44,678,800 bytes.
Each PTE and
the immutable voice prefix/BOS assets occupy nonoverlapping addresses in the
first 64 MiB OSPI partition. The fitted flash reports 128 MiB on hardware.
Release b-34/d-35 verified complete CRCs of the PTE (`a5f23dc5`), voice prefix
(`9d869927`) and BOS (`dc5d06f4`), including model data beyond 32 MiB. The
512-position graph needs a shared 38,801,424-byte planned arena plus
14,689,792 bytes of persistent state, before runtime/application overhead.
The fitted HyperRAM passed complete 64 MiB address-dependent write/readback
with cache clean/invalidate. Both dies report fixed initial latency 7; the
controller wait fields must use 7. A fresh reset-derived configuration repeats
the complete CRC and RAM checks without debugger register overrides.

At verified 100 MHz flash/RAM clocks, Shared_Sram takes **18.483 seconds per
80 ms audio frame** (two uninterrupted samples; RTF 231.0375). Dedicated_Sram
with 384 KiB of separate fast SRAM takes **18.4595 seconds** (RTF 230.74375).
At 25 MHz, both Shared frames take 59.345 seconds (RTF 741.8125). All 61
delegates execute and complete flash CRC/RAM readback pass, but frame outputs
disagree with the saved PTQ reference. Speech quality is not accepted.

The Dedicated run spends 18.057 seconds per frame outside backend delegate
execution, versus 401/404 ms inside the 61 backend calls. The latter includes
backend CPU copies and NPU wait; neither category is a pure CPU/NPU counter.
Aggregate backend tick rounding is bounded by about 61 ms. More NPU scratch
SRAM cannot make this current pipeline realtime. The required UART-to-audio
demo and optional LCD are deferred; resumption needs a different state/operator
and data-movement design, numerical acceptance and new complete-path timing.

Full synthesis runtime peaks are 165992 bytes for method metadata and 1231360
bytes for temporary scratch, in addition to the planned arena, persistent state
and application. Actual UART/audio/LCD application memory has not been measured.
See [benchmark.yml](model/benchmark.yml), [hardware evidence](model/hardware_bringup.json)
and [memory-mode comparisons](MEMORY_MODES.md) for scope, placements and limits.

The required application receives text via UART and synthesizes audio for
playback. An animated waveform on the landscape LCD is optional after measuring
RAM and cycle headroom. Use q8 only and store model constants in OSPI. Evaluate
Vela dedicated SRAM mode if shared SRAM cannot meet realtime deadlines, while
accounting for application RAM and any verified PSRAM backing.

For programming operations that exceed the tool timeout, the host-only
[chunk preparation helper](model/prepare_flash_chunks.py) first validates the
current generated layer/configuration/caches/assets, then emits 8 MiB parts and
one CMSIS run file per part:

```powershell
.venv/Scripts/python.exe projects/phonon_31m_en/model/prepare_flash_chunks.py --run out/EthosCatalog+DevKit-E8.cbuild-run.yml
```

Use the built/published bounded loader and the ProgramModel handoff, then program
the run files listed in `out/phonon-flash-parts/manifest.json` in order through
CMSIS flash. Parts start at 128 KiB erase boundaries and cannot share an erased
sector. The manifest records full-image/chunk hashes and loader provenance.
Wait for an active operation even if its response times out. Reload Release
after programming and require all three complete-image CRCs before model use.
Host reconstruction and guard tests pass. Full-image programming with the current
loader passed complete target CRC checks; the chunk fallback has not been needed
or exercised on hardware.
Regenerate parts after changing the PTE, caches, configuration, assets or loader.

See [CHECKPOINT.md](CHECKPOINT.md) for work state and
[CONVERSION.md](CONVERSION.md) for source provenance and numerical validation.
