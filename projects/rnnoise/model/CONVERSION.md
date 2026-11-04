# RNNoise INT8 conversion

The source is the ML-zoo pre-Vela INT8 RNNoise model, pinned to commit
`fec0bb5bcd486eb2839cdb7d437d466067402a6a`. Model SHA256 is
`9c582545b7c13af44616c44b654f4fe721aa2585630b0ca173ca3589f6f11c2c`.
The original artifact, source architecture, Python feature implementation,
nine reference arrays and Apache-2.0 license are preserved under `upstream/`.
`upstream/provenance.json` records URLs and SHA256 values. No original FP32
checkpoint, training corpus or additional inference dependency was downloaded.

`model.py` imports the 68 original TFLite operations into a static PyTorch graph.
It preserves the INT8 weights, INT32 biases, all activation scales/zero points,
the recurrent equations and the input/output ordering. Dense operators use an
equivalent 1x1 convolution expression because the original `linear` expression
left a CPU operator unavailable in the runtime pack. Repeated per-channel scales
have the same values as the original per-tensor scales and retain the source
float32 representation. No retraining, calibration or new PTQ is performed.
ExecuTorch chooses all CPU/NPU partitions automatically; there is no manual
subnetwork split. Final delegation is recorded in `../ai_layer/generation.json`.
The current PTE is 217,048 bytes and has two Ethos-U delegates. All nine dense
operations are delegated. The CPU remainder is one add/ReLU section and
quantization boundaries; this is a hybrid model. `vela_report.txt` preserves
both delegate compiler summaries: 24,684 and 62,994 MACs respectively (including
compiler-counted elementwise work). The original dense matrices total 86,952
MACs. Board benchmarks must include the CPU remainder and all delegate overhead.

The initial 168,040-byte, seven-delegate artifact left dense operations on the
CPU and measured 35.0492 ms per forward. Its benchmark is preserved as a
baseline in `BENCHMARK.md`/`benchmark.yml` until the current artifact is measured.
That timing does not describe the current model. Investigation showed that
explicit Q/DQ boundaries alone do not satisfy ExecuTorch 1.4.1's Arm
`CheckArmQuantized`: floating ATen compute also needs the Arm annotation.
`quantize.py` now checks the actual Q/DQ inputs/outputs of each compute node,
then applies the official `mark_node_as_annotated` helper. Integer structural
nodes are checked for integer output types. Unknown floating compute fails
the import instead of being marked blindly. The metadata survives PT2 saving
and loading; `conversion_annotations.json` records the annotation counts.

With these annotations, heavy compute reached Vela, exposing an Arm backend
`FuseEqualPlaceholdersPass` failure: `KeyError: constant_14` while deleting
duplicate constant placeholders. The scalar quantized-one buffer was shared
by three GRU gate subtractions. Giving each gate a shape-specific copy of the
same quantized value avoids the compiler failure while preserving arithmetic,
scales, state API and all measured host outputs. No imported code was edited.
The current model must be benchmarked again; NPU fixed-point rounding may differ
from the former CPU-only arithmetic, so board fidelity must also be rechecked.

## Firmware contract

All inputs and outputs are signed INT8, contiguous, batch size one. Input order:

| Index | Input | Shape | Scale | Zero point |
| --- | --- | --- | --- | --- |
| 0 | 42 features | 1 x 1 x 42 | 0.22150060534477234 | 14 |
| 1 | VAD state | 1 x 24 | 0.007843135856091976 | -1 |
| 2 | Noise state | 1 x 48 | 0.04794248938560486 | -128 |
| 3 | Denoise state | 1 x 96 | 0.007843135856091976 | -1 |

Outputs are denoise state (1 x 1 x 96), 22 gains (1 x 1 x 22), noise state
(1 x 1 x 48), VAD state (1 x 1 x 24), voice probability (1 x 1 x 1).
State output quantization matches its corresponding input, so copy output
3 -> input 1, output 2 -> input 2, output 0 -> input 3 without requantization.
Reset each state to its zero point, rather than all zero bytes. Features use
`clip(round(features / scale) + zero_point, -128, 127)`; gain and probability
decode as `(int8_value + 128) / 256`. Process 480 mono signed PCM samples at
48 kHz per frame. The Python frontend expects PCM in signed-int16 amplitude
units, converted to float, rather than normalized [-1, 1] audio.

## Validation and limits

`validate_conversion.py` verifies that TensorFlow Lite's reference integer
interpreter reproduces all nine upstream fixtures exactly. It compares PyTorch
with that interpreter and generates a continuous 160-frame (1.6 second) stream:
16 silence frames, 112 amplitude-modulated 173/346 Hz harmonic-tone plus seeded
noise frames, and 32 trailing silence frames. Both models independently feed
back their recurrent states. This is a deterministic conversion smoke test;
it does not measure speech enhancement quality or labelled VAD accuracy.

The single upstream fixture differs from imported PyTorch by maximum output
LSBs `[25, 8, 2, 0, 0]`, in the output order above. An intermediate check found
that every FC1/VAD tensor before the first coarse requantization matched the
source exactly. The first difference is one LSB at TFLite tensor 111, which
requantizes tanh output (scale 0.0078125) to concatenation scale 0.2215006053.
TFLite's fixed-point two-stage rounding and PyTorch's ideal Q/DQ rounding can
disagree there; the subsequent coarse noise-input quantization and recurrent
GRUs amplify this difference. The conversion is not bit exact.

Across the independently propagated sequence, gain absolute error is mean
0.0239591, maximum 0.14453125; voice-probability error is mean 0.00576172,
maximum 0.078125. Denoise/noise/VAD state maximum differences are respectively
103/10/23 LSB (40.39%/3.92%/9.02% of each full 255-step tensor range).
When each one-step comparison instead shares the original TFLite state,
gain error is mean 1.88011 LSB (0.00734419), maximum 16 LSB (0.0625),
and voice probability matches exactly. `conversion_metrics.json` and
`network.yml` contain all scales, normalized errors and measurements.

Removing quantization from the recovered INT8 weights gives gain error versus
imported INT8 of mean 0.0224703 and maximum 0.266938 on the same sequence.
That reconstructed float network is not the unavailable original FP32 model.
Upstream does not publish a float-versus-INT8 quantization error. Its reported
PESQ 2.945 is an offline source-model result, not a preserved score verified
by this port. No claim of product-level enhancement accuracy is made.

`frontend_reference.npz` contains int16 PCM, float32 features and silence flags
from the original Python frontend. Its public `process_frame` has a silence
branch returning two values although its caller unpacks seven; the validation
adapter consumes the first two feature results directly, leaving the imported
file unchanged. `recurrent_reference.npz` adds flattened PyTorch/TFLite outputs.
`model_test_data.h` supplies the upstream input, expected imported output and
source output for board startup checks. `sequence_test_data.h`, when generated,
supplies 160 quantized feature frames and both complete expected output streams.
Board execution must measure its own numerical differences and real inference
time; host conversion results alone do not establish either.

## Reproduce

From repository root in its configured Python environment:

```powershell
.venv/Scripts/python.exe projects/rnnoise/model/validate_conversion.py
.venv/Scripts/python.exe scripts/create_ai_layer.py --project rnnoise --quantize
.venv/Scripts/python.exe scripts/validate_networks.py projects/rnnoise/model/network.yml
```

The validation script additionally requires the existing TensorFlow installation
for its reference interpreter; TensorFlow is not part of the firmware. Generated
`.pt2` and `.safetensors` caches are pre-Vela, while `../ai_layer/model.pte` is the
compiled ExecuTorch artifact. Refresh the project-local AI layer after changing
the importer, source weights, quantization metadata or Vela/storage settings.
