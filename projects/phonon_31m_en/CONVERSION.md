# Phonon 31m English q8 conversion

Status: complete trained q8 import, streaming PyTorch reconstruction, pre-Vela
method exports and complete ExecuTorch/Vela lowering. Hardware tests fail
realtime and numerical acceptance in both SRAM modes. The Release application
is a retained model feasibility benchmark; it does not implement UART
text tokenization or physical audio playback.

## Exact source

- Model snapshot: `gradium/phonon-31m-en` at
  `52433c46975bfa13144c56204c9f4ee1c5b148aa`.
- Production file: `model.q8.gguf`, 64,512,352 bytes, SHA-256
  `a7d9aad2cd6dc66473f24a08bc6d2939030ef4d751ef82349361fbef3272dbae`.
- Architecture reference: Gradium `xn-ptts` v0.4.0,
  `f07d7952db0c6127d5e4bc3f41c82b3304b6b5aa`.
- All 185 tensors imported, 41,096,035 values: flow LM 30,790,722 and
  Mimi decoder 10,305,313. Q8_0 covers only 18,874,368 transformer values.
  Other stored values use GGML type 30, **BF16**, rather than float32.
- The 31M headline excludes the audio tokenizer. See
  [tensor inventory](model/tensor_inventory.json) for every shape/type/offset.
- No q4 or already Vela-compiled weights are used. The original q8 scheme is
  not Ethos int8; separate PTQ is necessary. [Notices](NOTICE.md) cover reuse.

`model/import_q8.py --download` fetches exact snapshot files, converts Q8_0
blocks (`fp16 scale * signed byte`) and BF16 values to float32 PyTorch tensors,
and saves `model/weights/phonon_q8_source.safetensors`. All 185 tensor names,
shapes and values survive a bit-exact safetensors round-trip. Source cache
SHA-256 is `156814c6ca16a67713311572f0fc6780d96abca4a09ca2f3d06d495f3c781c2a`.
This is numerical container conversion, not a claim of TTS output quality.

## Model and validation

`model/model.py` reconstructs the six flow-transformer layers, summed speaker
count/duration conditions, six-block AdaLN flow head, EOS head, and full Mimi
projection, depthwise upsampling, two transformer layers and SEANet decoder.
The supplied default voice uses the `speaker_wavs` tensor and trained speaker
projection. The unused training-only `flow.w_s_t` head remains in the complete
source cache and is omitted from inference, matching the upstream loader.

One frame generates 32 latent values and 1,920 mono samples at 24 kHz, an
80 ms audio deadline. Inference state is explicit. Default flow KV capacity
512 includes the 125-token default voice, text tokens and autoregressive
steps; the host must reject/split sequences before exceeding that capacity.
Mimi keeps a 250-position causal context and an absolute RoPE offset. It
advances 16 internal positions per latent frame.

Default persistent float32 state occupies **14,689,792 bytes**: flow KV
12,582,912; Mimi KV 2,048,000; convolution overlap 58,880. Input/output state
copies and the ExecuTorch/Vela arena can raise the peak further. This exceeds
the 8 MiB bulk SRAM before application buffers, so this representation needs
verified PSRAM placement. After the default voice and `N` text tokens, at most
`512 - 125 - N` autoregressive frames remain, each representing 80 ms.

The complete frame and shared-backbone inference methods export to pre-Vela
`.pt2` graphs. Splitting the eager frame into shared methods changes the latent,
EOS and PCM outputs by zero on the checked initial state. `model/validate.py` independently checks native PyTorch LayerNorm,
SDPA and complex RoPE against the streaming implementation, and a nonstreaming
causal decoder against chunked PCM, including the Mimi window rollover:

| Check | Maximum absolute error |
|---|---:|
| Flow attention, 20 tokens | 0.0000007153 |
| Full Mimi PCM, 18 frames | 0.0000040531 |
| SentencePiece vs published JSON tokenizer, four texts | identical token IDs |

These checks verify reconstructed formulas and state continuity. They do not
establish parity with the upstream executable or perceptual speech accuracy.
See [validation metrics](model/validation_metrics.json).

## Exported neural methods

`model/export_stages.py` writes three pre-Vela `.pt2` and `.safetensors` pairs,
then optionally lowers one PTE. `model/method_manifest.json` records all cache,
startup and implementation hashes. No backbone weights are duplicated for
prompt processing.

| Method | Inputs | Outputs |
|---|---|---|
| `text_embedding` (CPU) | one int32 token `[1,1]` | float embedding `[1,1,512]` |
| `backbone` | embedding `[1,1,512]`, Flow KV, absolute float position | hidden output and next Flow KV |
| `synthesize` | hidden output, noise `[1,32]`, Mimi KV/position and 12 convolution states | latent, raw EOS logit, PCM, next conditioned embedding, next Mimi/convolution state |

Prefill the 125 projected default-voice embeddings, then every text embedding,
using `backbone`. Start generation with the derived BOS embedding. Each frame
calls `backbone`, then `synthesize`; the returned next embedding is consumed
on the following frame. The EOS decision belongs to the current decoded frame.
Use EOS threshold 4 and decode three further frames for at most four words,
otherwise one further frame. The exported call signatures flatten the twelve
convolution-state tensors; the Python tuple is not an opaque firmware input.
`model/weights/startup.safetensors` (258,328 bytes) contains the exact derived
voice and BOS embeddings, with pinned-source provenance. This default-voice
application does not implement voice cloning.

For fast default-voice startup, `model/startup_assets.py` derives the final
quantized backbone's complete KV after all 125 voice tokens. Its safetensors
and raw float32 forms match explicit prefill bit-for-bit, including a checked
subsequent text-token output/cache. The manifest binds the prefix to exact
backbone graph/cache, q8 GGUF and default-voice hashes. Raw layout is:

| Asset | OSPI address | Size | SHA-256 |
|---|---|---:|---|
| `model/weights/voice_prefix.bin` | `0xC3000000` | 12,582,912 | `45bcb0df2bdc153a47568847dafa7a3b14c68fdb7d3b73087ce0ad6ea51deb2b` |
| `model/weights/bos_embedding.bin` | `0xC3C00000` | 2,048 | `cd56ca9e3fcbb90c9f8d7bc995339a2aaeb0560d16617604f8113e3003d326b3` |

The prefix is contiguous little-endian float32 `[6,2,1,8,512,64]`; BOS is
`[1,1,512]`. At utterance reset, copy the read-only prefix from OSPI to writable
Flow state and set its position to 125. Clear Mimi KV/convolution state and
set Mimi position to zero. Continue with normal text prefill, then BOS-led
generation. This avoids 125 voice-prefill calls per utterance; the initial
12.58 MB OSPI-to-PSRAM copy still counts toward startup timing. Do not mutate
the OSPI prefix. End of the final asset is `0xC3C00800`, inside the 64 MiB
partition. Full explicit voice prefill remains available through `backbone`.

Text normalization and SentencePiece remain application work. Upstream trims,
collapses whitespace, uppercases the first character and adds a period if the
last character is alphanumeric. Text chunking must respect the token/state
budget. Neither prompt compute nor tokenizer time may be omitted from the
whole application benchmark.

## Additional Ethos PTQ

The release's GGUF Q8_0 blocks are dequantized exactly, then independently
requantized for Ethos. The selected policy uses symmetric int8 linear and
convolution weights, int16 activations at those operators, and float32
attention, causal masks, normalization and recurrent state. Token lookup and
small floating constants remain float32. No four-bit weights are produced.
Per-channel scales are used for regular linear/convolution weights; transpose
weights use a per-tensor scale. Expressing the 1D transpose as a mathematically
equivalent singleton-height 2D transpose allows annotation and NPU delegation.
SiLU is expressed as `x * sigmoid(x)` because the installed runtime lacks SiLU;
its checked maximum absolute difference from native PyTorch is 9.54e-7.

Calibration uses the exact trained default voice, seeded temperature-0.8 noise,
all voice/text prompt steps and 61 complete voiced, midstream and EOS frames
from two English utterances. MinMax observers use epsilon 2^-24. No external
calibration dataset or training is used. Numerical teacher-forced comparisons
hold all source states/inputs fixed. Free recurrence separately quantizes the
prompt and feeds generated state/latent outputs back to subsequent steps.

Initial global-int8 PTQ is rejected: quantizing the -1e9 attention mask
collapsed valid logit resolution and caused approximately 100% latent NRMSE.
Early-frame-only selective calibration also attenuated complete utterances
severely. These probes are retained as diagnostics, not accepted artifacts.
The selected complete-utterance calibration removes that amplitude collapse.

Selected PTQ versus exact q8 eager over 61 fixed-state synthesis samples:

| Output | Mean NRMSE | Maximum NRMSE | Maximum absolute error |
|---|---:|---:|---:|
| latent | 1.014% | 1.577% | 0.069275 |
| EOS logit | 0.0575% | 0.2175% | 0.021476 |
| PCM | 23.60% | 65.66% | 0.126226 |

PCM NRMSE grows on near-silent frames: the worst relative frame has reference
RMS 0.000101 and RMSE 0.0000666. For the 39 frames with reference RMS above
0.01, mean PCM NRMSE is 9.696%, maximum 34.26%. This is numerical error, not
a perceptual score. In free recurrence samplewise PCM NRMSE is about 100%;
phase/drift makes it unsuitable as the sole speech quality criterion.

Both calibration utterances preserve EOS frame and duration (1.84 and 3.04 s).
Phase-tolerant diagnostics report log-mel MAE 2.82/3.83 dB, mean mel-spectrum
cosine 0.9963/0.9966 and energy-envelope correlation 0.9968/0.9583. Median
estimated voiced pitch is 208.70 vs 208.70 Hz and 188.99 vs 186.77 Hz.
Three held-out texts and a different seed produce EOS in every case; duration
changes by -80 ms for one utterance and is identical for the other two. Their
log-mel MAE is 5.05/2.63/3.94 dB, envelope correlation 0.961/0.996/0.982 and
mean-spectrum cosine 0.998/0.996/0.965. These small-corpus signal tests do not
establish intelligibility, speaker similarity or perceptual quality.

`quality_accepted` remains **false**. Deterministic WAV fixtures, text, seed,
EOS sequence and SHA-256 hashes are recorded in `model/staged_metrics.json`
and `model/heldout_metrics.json`; phase-tolerant methods/settings/results are
in `model/audio_metrics.json` and `model/heldout_audio_metrics.json`. WAV files
are under ignored `model/weights/staged_*.wav` for review.

The pinned model card publishes no q8 conversion-error score. Validation-only
original trained weights from the same snapshot are `model.safetensors`,
102,823,920 bytes, SHA-256
`8a04cf833cfe69b67c2ec968b2d31244655b9accc84ade9c0ebcc176f2074607`.
`model/compare_source.py` compares all 185 tensors and 16 teacher-forced early
frames from two texts against them. Mean per-frame NRMSE is 0.7353% latent
and 5.2492% PCM; maximum absolute error is 0.152853 latent, 0.034273 EOS and
0.223193 PCM. These source-q8 errors are separate from additional Ethos PTQ.
See `model/source_quantization_metrics.json`; original weights are never the
production replacement for q8.

## Lowering and runtime memory

The converter's staged Shared_Sram artifact is
`model/artifacts/staged/phonon_Shared_Sram.pte`: **44,684,432 bytes**, SHA-256
`ecf04bb5f29d8b402612d64c2df3be76a6973cd710ac7affa6ea27222867c21a`.
It serializes 61 actual Ethos-U85-256 delegates (24 backbone, 37 synthesis),
8,305,424 bytes of ordinary constants and 36,114,848 bytes of delegate data.
The source caches are produced before any Vela conversion.

The repository CLI subsequently generated the hardware-programmed Shared_Sram
`projects/phonon_31m_en/ai_layer/model.pte`: **44,675,472 bytes**, SHA-256
`cf07d603198495055f45bdc6eae73d119823c0d196cff65e4bf84c849844fab7`.
It has the same three methods, 61 delegates and planned arena sizes. Record
the actual generated PTE hash for firmware/programming; do not substitute the
earlier staged serializer's hash.

Post-partition graph inspection estimates 85.69% of backbone dense MACs and
96.90% of synthesis dense MACs on Ethos (96.08% combined per audio frame).
The remaining 3,151,872 + 8,716,288 dense MACs are float attention BMMs.
Every trained linear, regular convolution and transposed convolution is
delegated. The fraction excludes elementwise work, cache copies, quantization,
backend setup and I/O; it is not a latency speedup or realtime claim.
CPU operators include softmax, BMM, means/rsqrt, trigonometry, GELU/ELU,
sigmoid, arithmetic/masking, state concatenation/views/copies, embedding and
quantize/dequantize boundaries. Exact operator/count lists and delegated graph
sizes are in `model/staged_metrics.json` and `model/lowering_Shared_Sram.json`.
The installed runtime must supply all listed operators and Ethos support.

Serialized planned memory buffers are 38,801,424 bytes for `backbone`,
6,716,144 for `synthesize`, and 2,064 for `text_embedding`. Together with the
14,689,792-byte external persistent state this is **60,209,424 bytes** if all
method arenas are retained separately, before runtime metadata, application,
NPU fast memory and alignment. The hardware harness now aliases one
38,801,424-byte HyperRAM arena across sequential methods and copies required
outputs before reuse. Aligned caller inputs/state occupy another 14,694,208
bytes. Full synthesis peaks are 165,992 bytes for method metadata and
1,231,360 bytes for temporary scratch, backed by 256 KiB and 1.5 MiB SRAM
pools. Application UART/audio/LCD memory is additional and unmeasured. Vela's
largest Shared_Sram per-delegate SRAM estimate is 1,202.50 KiB; CPU state
rolling/copy traffic can dominate even with high MAC delegation.

Dedicated_Sram compilation uses the same pre-Vela caches and configured
393,216-byte arena cache; see `model/lower_stages.py` and its resulting report.
The earlier staged converter PTE is 44,687,888 bytes, SHA-256
`af8a9eca89320c0eec66c8b314686fb14cdd3db15cbff39b1fb9289906d7d0d0`,
with the same 61 delegates, CPU operations and planned buffers. Maximum
per-delegate Vela SRAM usage is 383.59 KiB, versus Shared_Sram 1,202.50 KiB.
This fast-cache estimate does not replace the runtime's slow scratch backing:
both serialized modes still request up to 1,231,360 bytes for synthesis slow
scratch (5,152 bytes for backbone). Dedicated hardware execution also requires
a strong `ethosu_fast_scratch` pointer/size with 393,216 verified SRAM bytes;
the backend's weak defaults are null/zero. Budget both allocations and the
application, and verify each NPU region's address routing.
The actual repository CLI Dedicated_Sram PTE is 44,678,800 bytes, SHA-256
`a2f0a57d8b2f7d628aeeea7f391c2070445b215e4398fed967b6161363b90dd5`,
CRC `b8bb566c`. Host preflight passes from unchanged caches/assets/fixtures.
Release b-59/d-60 passes full flash CRC/RAM gates at 100 MHz, but two
uninterrupted frames average 18459.5 ms for an 80 ms deadline (RTF 230.74375).
Backend execute intervals total 401/404 ms; 18057 ms per frame remains outside
those intervals. First-token and full-frame numerical comparisons fail.
Vela memory and bandwidth assumptions are not measurements of the populated
board. See MEMORY_MODES.md and model/dedicated_100mhz_result.json.

The PTE fits the new 64 MiB configured OSPI model partition. Parent verified
the populated flash over CMSIS: JEDEC 9D5B1B, extended ID 1, capacity 128 MiB.
The expanded loader programmed the complete PTE and startup assets; Release
b-34/d-35 verified full CRCs `a5f23dc5`, `9d869927`, `dc5d06f4`, including
addresses above 32 MiB. See [hardware evidence](model/hardware_bringup.json).
Tokenizer storage and writable runtime state must also be budgeted. External
RAM passed full 64 MiB address-dependent physical readback, and all three PTE
methods loaded. The latest Shared_Sram measurements take 59.345 seconds per
80 ms frame at 25 MHz external clocks and 18.483 seconds at 100 MHz (two
uninterrupted frames per configuration; RTF 741.8125 and 231.0375). The GELU
component requires -fno-fast-math and first-token repeatability remains open;
full-frame PTQ differences are unaccepted. See NUMERICAL_ISOLATION.md,
`model/benchmark.yml` and `model/hardware_bringup.json`. Realtime
requires complete-path wall time/audio duration <= 1 and sustained 80 ms frame
deadlines including CPU fallback, prompt/tokenizer work, state traffic and
playback buffering. Optional landscape LCD animation is conditional on measured
remaining memory and cycles.

## Reproduction

From the repository Python environment, run:

```text
python projects/phonon_31m_en/model/import_q8.py --download
python projects/phonon_31m_en/model/validate.py
python projects/phonon_31m_en/model/compare_source.py --download-reference
python projects/phonon_31m_en/model/export_stages.py --lower
python projects/phonon_31m_en/model/startup_assets.py
python projects/phonon_31m_en/model/benchmark_fixtures.py
python projects/phonon_31m_en/model/first_token_reference.py
python projects/phonon_31m_en/model/evaluate_stages.py
python projects/phonon_31m_en/model/audio_metrics.py
python projects/phonon_31m_en/model/audio_metrics.py --report projects/phonon_31m_en/model/heldout_metrics.json --output projects/phonon_31m_en/model/heldout_audio_metrics.json
python projects/phonon_31m_en/model/lower_stages.py --memory-mode Dedicated_Sram
```

`export_stages.py --flow-capacity 256` provides a bounded-context memory/traffic
experiment. The current accepted-for-feasibility caches use capacity 512;
a smaller export must reject/split any sequence exceeding its complete
voice+text+generation budget, never silently truncate context. All scripts
write model artifacts and do not regenerate the project's `ai_layer/`.

The source weights, method caches and generated startup binaries are ignored
by Git. On a fresh checkout run the import/export/startup commands before
generating the project AI layer; the existing benchmark fixture header is
bound to the recorded caches, so regenerate it after any export. AI layer generation
uses `scripts/create_ai_layer.py --project projects/phonon_31m_en/phonon_31m_en.cproject.yml`.
Select and build only the Phonon project through CMSIS, then program and verify
the exact resulting PTE/assets before model use. A successful host export is
not a target RAM, inference-quality or realtime acceptance result.
